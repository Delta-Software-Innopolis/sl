syntax keyword slKeyword var type routine if then else end while for loop in reverse record array
syntax keyword slBoolean true false
syntax keyword slType integer real boolean
syntax keyword slFunction print_integer print_boolean print_real
syntax match slOperator ":=\|=>\|\.\.\|<=\|>=\|/=\|[+\-*/%=<>]"
syntax match slNumber "\<\d\+\(\.\d\+\)\?\>"
syntax match slComment "//.*$"
syntax match slComma ","
syntax match slSemicolon ";"
syntax match slParens "(\|)\|\[\|\]"
syntax match slFunctionCall "\<\h\w*\>\ze\s*("
syntax region slParamList start="routine\s\+\h\w*\s*(" end=")" contains=slParamName,slType transparent

highlight link slKeyword Keyword
highlight link slBoolean Boolean
highlight link slType Type
highlight link slFunction Function 
highlight link slOperator Operator
highlight link slNumber Number
highlight link slComment Comment
highlight link slComma Special
highlight link slParens Special
highlight link slSemicolon Special
highlight link slFunctionCall Function
