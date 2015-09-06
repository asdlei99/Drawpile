# Try to find QtColorWidgets (Qt5 version) (https://github.com/mbasaglia/Qt-Color-Widgets)
# 
# This will define
# LIBQTCOLORWIDGETS_FOUND
# LIBQTCOLORWIDGETS_INCLUDE_DIRS
# LIBQTCOLORWIDGETS_LIBRARIES

find_path(LIBQTCOLORWIDGETS_INCLUDE_DIR color_wheel.hpp
	PATH_SUFFIXES QtColorWidgets)
find_library(LIBQTCOLORWIDGETS_LIBRARY NAMES ColorWidgets-qt5)

set(LIBQTCOLORWIDGETS_INCLUDE_DIRS ${LIBQTCOLORWIDGETS_INCLUDE_DIR})
set(LIBQTCOLORWIDGETS_LIBRARIES ${LIBQTCOLORWIDGETS_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(QtColorWidgets DEFAULT_MSG LIBQTCOLORWIDGETS_LIBRARY LIBQTCOLORWIDGETS_INCLUDE_DIR)

mark_as_advanced(LIBQTCOLORWIDGETS_INCLUDE_DIR LIBQTCOLORWIDGETS_LIBRARY)

