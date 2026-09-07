setlocal commentstring=//\ %s
setlocal expandtab shiftwidth=4 tabstop=4 softtabstop=4
setlocal iskeyword+=_
let b:match_words = '\<\%(if\|while\|for\|routine\)\>:\<\%(then\|else\|loop\)\>:\<end\>'
