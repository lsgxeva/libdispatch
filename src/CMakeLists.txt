set (core_sources
  apply.c
  benchmark.c
  data.c
  data_additions.c
  data_internal.h
  init.c
  internal.h
  io.c
  io_additions.c
  io_internal.h
  object.c
  object_internal.h
  once.c
  queue.c
  queue_internal.h
  semaphore.c
  semaphore_internal.h
  shims.h
  source.c
  source_internal.h
  time.c
  trace.h
  transform.c
)

set(shims
  shims/atomic.h
  shims/getprogname.h
  shims/hw_config.h
  shims/malloc_zone.h
  shims/perfmon.h
  shims/pthread_main_np.c
  shims/pthread_main_np.h
  shims/queue.h
  shims/sleep.h
  shims/snprintf.h
  shims/strlcpy.h
  shims/time.h
  shims/tsd.h
)

set (private_headers
  ../private/benchmark.h
  ../private/data_private.h
  ../private/dispatch.h
  ../private/private.h
  ../private/queue_private.h
  ../private/source_private.h
)

set (public_headers
  ../dispatch/base.h
  ../dispatch/data.h
  ../dispatch/dispatch.h
  ../dispatch/group.h
  ../dispatch/io.h
  ../dispatch/object.h
  ../dispatch/once.h
  ../dispatch/queue.h
  ../dispatch/semaphore.h
  ../dispatch/source.h
  ../dispatch/time.h
)

set (sources ${core_sources} ${shims} ${private_headers} ${public_headers})

source_group("Core" FILES ${core_sources})
source_group("Core\\Shims" FILES ${shims})
source_group("Headers\\Private" FILES ${private_headers})
source_group("Headers\\Public" FILES ${public_headers})

if (NOT CBLOCKS_COMPILER_SUPPORT_FOUND)
  # Don't compile the following if we don't have blocks:
  set_source_files_properties(
      data.c
      data_additions.c
      data_internal.h
      io.c
      io_additions.c
      io_internal.h
      transform.c
    PROPERTIES LANGUAGE "")
endif ()

function (include_if)
  foreach (arg IN LISTS ${ARGV})
    if (${arg})
      include_directories("${arg}")
    endif ()
  endforeach ()
endfunction ()

if (USE_MIG)
  list(APPEND sources
    protocol.defs
    provider.d
  )
endif ()

include_directories (
  "${dispatch_SOURCE_DIR}"
  "${dispatch_BINARY_DIR}"
  "${dispatch_SOURCE_DIR}/private"
  "${dispatch_SOURCE_DIR}/os"
)

include_if(
  "${KQUEUE_INCLUDE_DIRS}"
  "${PTHREAD_WORKQUEUE_INCLUDE_DIRS}"
  "${CBLOCKS_INCLUDE_DIRS}"
)


if (HAVE_DARWIN_LD)
  list (APPEND linker_flags 
    "-Wl,-compatibility_version,1"
    "-Wl,-current_version,${dispatch_VERSION}"
    "-Wl,-dead_strip"
  )
endif ()

if (MSVC)
  set (cflags
    -W4
    #disabled warnings:
    -wd4068  #unknown pragma
    -wd4201  # anonymous struct/union
    -wd4100  # unreferenced formal parameter
    -wd4127  # conditional is constant
    -wd4324  # padding added
    -wd4101  # unreferenced local variable
    -wd4221  # designated init fussiness
    -wd4054 -wd4152  # function to pointer
    -wd4706  # assignment in conditional
  )
else ()
  set (cflags
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-unknown-pragmas
    ${VISIBILITY_FLAGS}
    ${OMIT_LEAF_FP_FLAGS}
    ${CBLOCKS_COMPILE_FLAGS}
  )
endif ()

if (USE_OBJC)
  list (APPEND sources object.m)
  DSAddCompilerFlags(SOURCE object.m FLAGS "-fobjc-gc")
  
  list (APPEND linker_flags
    -Wl,-upward-lobjc
    -Wl,-upward-lauto
    -Wl,-order_file,${dispatch_SOURCE_DIR}/xcodeconfig/libdispatch.order
    -Wl,-alias_list,${dispatch_SOURCE_DIR}/xcodeconfig/libdispatch.aliases
    -Wl,-unexported_symbols_list,${dispatch_SOURCE_DIR}/xcodeconfig/libdispatch.unexport
  )
endif ()


# if (USE_MIG)
#   set (BUILD_SOURCES
#     protocolUser.c
#     protocol.h
#     protocolServer.c
#     protocolServer.h
#   )
#
#   %User.c %.h %Server.c %Server.h: ${abs_srcdir}/%.defs
#     ${MIG} -user $*User.c -header $*.h
#         -server $*Server.c -sheader $*Server.h $<
# endif ()

add_library(libdispatch_static STATIC ${sources})
add_library(libdispatch_shared SHARED ${sources})

DSAddCompilerFlags(TARGET libdispatch_static libdispatch_shared
  FLAGS ${cflags})

if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_DEPRECATE)
endif()

DSTargetAddLinkerFlags(TARGET libdispatch_static libdispatch_shared
  FLAGS ${linker_flags})

foreach (target libdispatch_shared libdispatch_static)
  set (libs
    ${CMAKE_THREAD_LIBS_INIT}
    ${PTHREAD_WORKQUEUE_LIBRARIES}
    ${KQUEUE_LIBRARIES}
    ${CBLOCKS_LIBRARIES}
    ${CLOCK_GETTIME_LIBRARIES}
  )

  foreach (lib IN LISTS libs)
    if (${lib})
      target_link_libraries(${target} ${lib})
    endif ()
  endforeach ()
endforeach ()

set_property(TARGET libdispatch_static libdispatch_shared PROPERTY
  OUTPUT_NAME dispatch)

set_target_properties(libdispatch_shared PROPERTIES 
  SOVERSION 0
  VERSION ${dispatch_VERSION}
)

install(
  TARGETS libdispatch_shared libdispatch_static
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
