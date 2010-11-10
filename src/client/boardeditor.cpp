/*
   DrawPile - a collaborative drawing program.

   Copyright (C) 2006-2009 Calle Laakkonen

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <QDebug>
#include <QColor>

#include "boardeditor.h"
#include "boarditem.h"
#include "board.h"
#include "user.h"
#include "interfaces.h"
#include "sessionstate.h"
#include "preview.h"
#include "annotationitem.h"
#include "core/layer.h"
#include "core/layerstack.h"
#include "../shared/net/annotation.h"

namespace drawingboard {

/**
 * @param board drawing board to edit
 * @param user user to commit the changes as
 * @param brush brush source
 * @param color color source
 */
BoardEditor::BoardEditor(Board *board, User *user,
		interface::BrushSource *brush,
		interface::ColorSource *color)
	: user_(user), board_(board), brush_(brush), color_(color)
{
	Q_ASSERT(board);
	Q_ASSERT(user);
	Q_ASSERT(brush);
	Q_ASSERT(color);
}

BoardEditor::~BoardEditor() { }

/**
 * @return brush generated by the brush source object
 */
dpcore::Brush BoardEditor::localBrush() const
{
	return brush_->getBrush();
}

/**
 * @param color color to set as foreground
 */
void BoardEditor::setLocalForeground(const QColor& color)
{
	color_->setForeground(color);
}

/**
 * @param color color to set as background
 */
void BoardEditor::setLocalBackground(const QColor& color)
{
	color_->setBackground(color);
}

/**
 * @return color at specified coordinates
 * @retval invalid color if point was outside the board
 */
QColor BoardEditor::colorAt(const QPoint& point)
{
	if(board_->image_ == 0)
		return QColor();
	return board_->image_->image()->colorAt(point.x(), point.y());
}

/**
 * @return annotation at the specified coordinates or null if none found
 */
AnnotationItem *BoardEditor::annotationAt(const QPoint& point)
{
	QList<QGraphicsItem*> items = board_->items(point);
	foreach(QGraphicsItem *i, items) {
		if(i->type() == AnnotationItem::Type)
			return static_cast<AnnotationItem*>(i);
	}
	return 0;
}

/**
 * @param tool tool type to use
 * @param point starting point
 * @param brush brush to preview with
 */
void BoardEditor::startPreview(tools::Type tool, const dpcore::Point& point, const dpcore::Brush& brush)
{
	Q_ASSERT(board_->toolpreview_ == 0);
	Q_ASSERT(tool == tools::LINE || tool == tools::RECTANGLE || tool == tools::ANNOTATION);
	if(tool == tools::LINE)
		board_->toolpreview_ = new StrokePreview(user_->board());
	else
		board_->toolpreview_ = new RectanglePreview(user_->board());
	board_->toolpreview_->preview(point,point, brush);
}

void BoardEditor::continuePreview(const dpcore::Point& point)
{
	Q_ASSERT(board_->toolpreview_);
	board_->toolpreview_->moveTo(point);
}

void BoardEditor::endPreview()
{
	Q_ASSERT(board_->toolpreview_);
	delete board_->toolpreview_;
	board_->toolpreview_ = 0;
}

/**
 * This is currently only used by the annotation tool.
 * @param x tile x index
 * @param y tile y index
 * @param layer layer to merge
 */
void BoardEditor::mergeLayer(int x, int y, const dpcore::Layer *layer)
{
	board_->image_->image()->getLayer(user_->layer())->merge(x, y, layer);
}

/**
 * @param brush to compare
 * @retval true if brush matches what the user is currently using
 */
bool BoardEditor::isCurrentBrush(const dpcore::Brush& brush) const
{
	return user_->brush() == brush;
}

/**
 * @param brush brush to use
 */
void LocalBoardEditor::setTool(const dpcore::Brush& brush)
{
	user_->setBrush(brush);
}

/**
 * @param id layer id
 */
void LocalBoardEditor::setLayer(int id)
{
	user_->setLayerId(id);
}

/**
 * A new empty layer is created on top of the layer stack
 * @param name layer name
 */
void LocalBoardEditor::createLayer(const QString& name)
{
	board_->addLayer(name);
	setLayer(board_->layers()->getLayerByIndex(board_->layers()->layers()-1)->id());
}

/**
 * @param id layer ID
 */
void LocalBoardEditor::deleteLayer(int id, bool mergedown)
{
	board_->deleteLayer(id, mergedown);
}

/**
 * @param id layer ID
 * @param toId ID of layer under which the layer is moved to
 */
void LocalBoardEditor::moveLayer(int id, int toId)
{
	board_->layers()->moveLayer(id, toId);
	board_->update();
}

/**
 * @param id layer ID
 * @param opacity new layer opacity
 */
void LocalBoardEditor::changeLayerOpacity(int id, int opacity)
{
	board_->layers()->getLayer(id)->setOpacity(opacity);
	board_->update();
}

/**
 * @param point point to add
 */
void LocalBoardEditor::addStroke(const dpcore::Point& point)
{
	user_->addStroke(point);
}

void LocalBoardEditor::endStroke()
{
	user_->endStroke();
}

void LocalBoardEditor::annotate(protocol::Annotation a)
{
	a.user = user_->id();
	if(a.id==0)
		a.id = AnnotationItem::nextId();
	board_->annotate(a);
}

void LocalBoardEditor::removeAnnotation(int id)
{
	board_->unannotate(id);
}

/**
 * @param board board to user
 * @param user user to draw as
 * @param session network session over which commands are transmitted
 * @param brush brush source
 * @param color color source
 */
RemoteBoardEditor::RemoteBoardEditor(Board *board, User *user,
		network::SessionState *session,
		interface::BrushSource *brush,
		interface::ColorSource *color)
	: BoardEditor(board, user, brush, color), session_(session),
	lastbrush_(0,0,0), atomic_(false)
{
	Q_ASSERT(session);
}

/**
 * Compare with a cached brush to avoid sending extra ToolInfo
 * messages
 * @param brush to compare
 * @retval true if brush matches what the user is currently using
 */
bool RemoteBoardEditor::isCurrentBrush(const dpcore::Brush& brush) const
{
	return lastbrush_ == brush;
}

/**
 * @param brush brush to set
 */
void RemoteBoardEditor::setTool(const dpcore::Brush& brush)
{
	lastbrush_ = brush;
	session_->sendToolSelect(brush);
}

/**
 * @param id layer id
 */
void RemoteBoardEditor::setLayer(int id)
{
	session_->sendLayerSelect(id);
}

void RemoteBoardEditor::createLayer(const QString& name)
{
	qWarning() << "TODO: layer creation";
	// Note. We don't select the newly created layer here
	// since it won't actually be created until the command
	// has returned from the server.
}

/**
 * Layer deletion is not supported while in a network session.
 * @param id layer ID
 */
void RemoteBoardEditor::deleteLayer(int id, bool mergedown)
{
	qWarning() << "BUG: Tried to delete layer ID" << id << "while in a network session!";
}

/**
 * @param id layer ID
 * @param toId ID of layer under which the layer is moved to
 */
void RemoteBoardEditor::moveLayer(int id, int toId)
{
	qWarning() << "TODO: layer reordering";
}

/**
 * @param id layer ID
 * @param opacity new layer opacity
 */
void RemoteBoardEditor::changeLayerOpacity(int id, int opacity)
{
	qWarning() << "TODO: layer opacity changing";
}

/**
 * In atomic mode, all strokes until strokeEnd are sent in a single long
 * message. This is slightly more efficient and ensures no brush strokes
 * from other users can be interleaved with the components of this stroke.
 */
void RemoteBoardEditor::startAtomic()
{
	atomic_ = true;
}

/**
 * @param point stroke coordinates
 */
void RemoteBoardEditor::addStroke(const dpcore::Point& point)
{
	if(atomic_)
		atomics_.append(point);
	else
		session_->sendStrokePoint(point);
	board_->addPreview(point);
}

void RemoteBoardEditor::endStroke()
{
	if(atomic_) {
		Q_ASSERT(atomics_.isEmpty()==false);
		session_->sendAtomicStroke(atomics_);
		atomics_.clear();
		atomic_ = false;
	}
	session_->sendStrokeEnd();
	board_->endPreview();
}

void RemoteBoardEditor::annotate(protocol::Annotation a)
{
	session_->sendAnnotation(a);
}

void RemoteBoardEditor::removeAnnotation(int id)
{
	session_->sendRmAnnotation(id);
}

}

