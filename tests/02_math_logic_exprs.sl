routine main() =
    var a: integer = 5
    var b: integer = 10

    var c: integer = a + b
    print_integer(c)
    var d: integer = a - b
    print_integer(d)
    var e: integer = a * b
    print_integer(e)
    var f: integer = b / a
    print_integer(f)
    var g: integer = b % a
    print_integer(g)
    print_integer(-a)

    var h: real = 7.5 + 2.5
    print_real(h)
    var i: real = 7.5 - 2.5
    print_real(i)
    var j: real = 2.0 * 3.0
    print_real(j)
    var k: real = 7.0 / 2.0
    print_real(k)

    print_boolean(a < b)
    print_boolean(a > b)
    print_boolean(a <= b)
    print_boolean(a >= b)
    print_boolean(a = b)
    print_boolean(a /= b)

    var p: boolean = true
    var q: boolean = false
    print_boolean(p and q)
    print_boolean(p or q)
    print_boolean(p xor q)
    print_boolean(not p)
end
