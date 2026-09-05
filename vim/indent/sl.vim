setlocal indentexpr=GetMylangIndent()
setlocal indentkeys+=0=end,0=else

function! GetMylangIndent()
  let prevlnum = prevnonblank(v:lnum - 1)
  if prevlnum == 0
    return 0
  endif

  let prevline = getline(prevlnum)
  let curline = getline(v:lnum)
  let ind = indent(prevlnum)

  if prevline =~ '\%(then\|loop\|else\)\s*$' || prevline =~ '=\s*$'
    let ind += shiftwidth()
  endif

  if curline =~ '^\s*\%(end\|else\)\>'
    let ind -= shiftwidth()
  endif

  return ind
endfunction
