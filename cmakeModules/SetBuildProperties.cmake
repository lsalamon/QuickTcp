MACRO(SetBuildProperties projectName)
	if(UNIX)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUNIX")
	else()
		set(MSVC_COMMON_FLAGS "-DWINDOWS /W3 /WX /GS /fp:precise /Zc:wchar_t /Zc:forScope /GR /Gd /TP /Oy-")
		set(ADDITIONAL_FLAGS_DEBUG "/Od /Ob0 /Gm /Zi -DDEBUG -D_ITERATOR_DEBUG_LEVEL=1 -D_SCL_SECURE_NO_WARNINGS")
		set(ADDITIONAL_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /Ot -D_ITERATOR_DEBUG_LEVEL=0 /Zi")
		set(ADDITIONAL_FLAGS_RELEASE "/O2 /Ob2 /Ot -D_ITERATOR_DEBUG_LEVEL=0")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${MSVC_COMMON_FLAGS} ${ADDITIONAL_FLAGS_RELEASE}")
		set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE} ${MSVC_COMMON_FLAGS} ${ADDITIONAL_FLAGS_RELWITHDEBINFO}")
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${MSVC_COMMON_FLAGS} ${ADDITIONAL_FLAGS_DEBUG}")
		if(BUILD_SHARED_LIBS)
			set(SHARED_COMMON_FLAGS "-DBUILD_SHARED_LIBS")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${SHARED_COMMON_FLAGS}")
			set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${SHARED_COMMON_FLAGS}")
			set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SHARED_COMMON_FLAGS}")
		endif()
		if(CMAKE_SIZEOF_VOID_P EQUAL 8) #64bit
			FOREACH(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
				CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
				STRING(REGEX REPLACE "-DWIN32" "-DWIN64" ${flag_var} "${${flag_var}}")
			ENDFOREACH(flag_var)
		endif()
		if(USE_STATIC_RUNTIME)
			FOREACH(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
				CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
				STRING(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
			ENDFOREACH(flag_var)
		endif()
	endif()
ENDMACRO()