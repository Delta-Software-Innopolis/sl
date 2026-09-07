routine main() =
    var i: integer = 5
    var r: real = 2.5

    var a: real = i + r
    print_real(a)

    var b: real = i - r
    print_real(b)

    var c: real = i * r
    print_real(c)

    var d: real = i / r
    print_real(d)

    var e: real = r + i
    print_real(e)

    var f: real = 10 + 0.5
    print_real(f)

    print_boolean(i < r)
    print_boolean(i > r)
    print_boolean(i = r)
    print_boolean(i /= r)

    print_boolean((i + r) = 7.5)
    print_boolean((i * r) > 12.0)
    print_boolean((i - r) <= 2.5)

    if (2 - 2) then
        print_boolean(true)
    else
        print_boolean(false)
    end

    if (3 - 2) then
        print_boolean(true)
    else
        print_boolean(false)
    end

    if ((10 / 5) - 2) then
        print_boolean(true)
    else
        print_boolean(false)
    end

    if (i + r > 7.0) then
        print_boolean(true)
    else
        print_boolean(false)
    end
end
