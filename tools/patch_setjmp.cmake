# Post-codegen fix: setjmp/longjmp calls that go through veneers.
#
# ReXGlue turns a call to setjmp_address / longjmp_address (overrides.toml)
# into an inline ppc_setjmp / ppc_longjmp, which has to happen in the caller's
# own frame. This game never calls them directly: every call goes through one
# of two long-branch veneers each, so the generator emitted plain calls and the
# guest longjmp in libjpeg's error handler restored guest registers without
# unwinding the host stack (crash: read of guest 0x1A4).
#
# This rewrites each call to those veneers in the generated sources into the
# same code ReXGlue emits for a direct call. It runs after every codegen: from
# setup.ps1 and from the codegen step of the build (ewj/CMakeLists.txt), on
# every platform. Running it twice is harmless (nothing left to rewrite).
#
#   cmake -DGENERATED=<ewj/generated/default> -P tools/patch_setjmp.cmake

if(NOT GENERATED)
  get_filename_component(GENERATED "${CMAKE_CURRENT_LIST_DIR}/../ewj/generated/default" ABSOLUTE)
endif()

set(_setjmp 827CFFC0 837E4D40)   # -> setjmp 0x83259AE0
set(_longjmp 82728C90 8373DA10)  # -> longjmp 0x832596B0

file(GLOB _sources "${GENERATED}/*_recomp.*.cpp")
set(_total 0)
foreach(_file IN LISTS _sources)
  file(READ "${_file}" _text)
  string(REGEX MATCHALL "// ewj: (set|long)jmp via veneer" _before "${_text}")
  list(LENGTH _before _before_n)
  set(_changed FALSE)
  foreach(_veneer IN LISTS _setjmp _longjmp)
    string(FIND "${_text}" "sub_${_veneer}(ctx, base);" _at)
    if(_at EQUAL -1)
      continue()
    endif()
    if(_veneer IN_LIST _longjmp)
      string(REGEX REPLACE "\n(\t+)sub_${_veneer}\\(ctx, base\\);"
             "\n\\1ppc_longjmp(ctx.r3.u32, ctx.r4.s32);  // ewj: longjmp via veneer ${_veneer}" _text "${_text}")
    else()
      # Same as BuilderContext::emit_function_call for setjmp_address.
      string(REGEX REPLACE "\n(\t+)sub_${_veneer}\\(ctx, base\\);"
             "\n\\1{  // ewj: setjmp via veneer ${_veneer}\n\\1\tPPCContext ewj_env = ctx;\n\\1\tPPCRegister ewj_ret{};\n\\1\tewj_ret.s64 = ppc_setjmp(ctx.r3.u32);\n\\1\tif (ewj_ret.s64 != 0) ctx = ewj_env;\n\\1\tctx.r3 = ewj_ret;\n\\1}"
             _text "${_text}")
    endif()
    set(_changed TRUE)
  endforeach()
  if(_changed)
    string(REGEX MATCHALL "// ewj: (set|long)jmp via veneer" _after "${_text}")
    list(LENGTH _after _after_n)
    math(EXPR _total "${_total} + ${_after_n} - ${_before_n}")
    file(WRITE "${_file}" "${_text}")
  endif()
endforeach()
message(STATUS "patch_setjmp: rewrote ${_total} call sites")
