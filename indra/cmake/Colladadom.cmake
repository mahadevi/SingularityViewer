# -*- cmake -*-

include(Prebuilt)

set(COLLADADOM_FIND_QUIETLY OFF)
set(COLLADADOM_FIND_REQUIRED ON)

if (STANDALONE)
  include (FindColladadom)
else (STANDALONE)
  use_prebuilt_binary(colladadom)

  if (NOT WINDOWS)
	use_prebuilt_binary(pcre)
  endif (NOT WINDOWS)

  if (NOT DARWIN AND NOT WINDOWS)
	use_prebuilt_binary(libxml)
  endif (NOT DARWIN AND NOT WINDOWS)

  set(COLLADADOM_INCLUDE_DIRS
	  ${LIBS_PREBUILT_DIR}/${LL_ARCH_DIR}/include/collada
	  ${LIBS_PREBUILT_DIR}/${LL_ARCH_DIR}/include/collada/1.4
	  )

  if (WINDOWS)
	  add_definitions(-DDOM_DYNAMIC)
	  set(COLLADADOM_LIBRARIES 
		  debug libcollada14dom22-d
		  optimized libcollada14dom22
		  debug libboost_filesystem-vc100-mt-gd-1_45.lib
		  optimized libboost_filesystem-vc100-mt-1_45.lib
		  debug libboost_system-vc100-mt-gd-1_45.lib
		  optimized libboost_system-vc100-mt-1_45.lib
		  )
  else (WINDOWS)
	  set(COLLADADOM_LIBRARIES 
		  collada14dom
		  minizip
		  xml2
		  pcrecpp
		  pcre
		  )
  endif (WINDOWS)
endif (STANDALONE)

