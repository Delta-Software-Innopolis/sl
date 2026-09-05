setlocal commentstring=//\ %s
setlocal expandtab shiftwidth=2 tabstop=2 softtabstop=2
setlocal iskeyword+=_
let b:match_words = '\<\%(if\|while\|for\|routine\)\>:\<\%(then\|else\|loop\)\>:\<end\>'
