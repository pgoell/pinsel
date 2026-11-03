# vcpkg's own x64-linux triplet, plus Tracy's network options: its port has no
# feature for them, and they must be set when the client builds.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# The client listens on localhost only and never announces itself on the network.
if(PORT STREQUAL "tracy")
  set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DTRACY_ONLY_LOCALHOST=ON -DTRACY_NO_BROADCAST=ON)
endif()
