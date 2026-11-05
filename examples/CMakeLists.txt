# Demonstration programs (OVERDRIVE_BUILD_EXAMPLES). Each uses the public
# API only, as a consumer would.

# hello_overdrive (T037, US2): run by hand per docs/validation/manual-checklist.md.
add_executable(hello_overdrive hello_overdrive.c)
target_link_libraries(hello_overdrive PRIVATE overdrive::overdrive)
ovd_target_warnings(hello_overdrive)
