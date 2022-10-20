#
# Address Sanitizer
#
if(ENABLE_ASAN)
    #
    # -fsanitize=address           - Build with address sanitizer support
    # -fno-omit-frame-pointer      - Always keep the frame pointer in a register
    # -fno-optimize-sibling-calls  - Don't optimize away tail recursion.
    #
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls")
    add_link_options("-fsanitize=address")
    message("ENABLING ASAN")
endif()

