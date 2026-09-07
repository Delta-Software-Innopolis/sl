routine main() =
    var arr: array[5] integer
    var i: integer = 0
    var sum: integer = 0

    while i < 5 loop
        arr[i] := i * i
        i := i + 1
    end

    for row in 0..2 loop
        for col in 0..2 loop
            if (row + col) % 2 = 0 then
                print_integer(row * 10 + col)
            else
                if row = col then
                    print_integer(-1)
                end
            end
        end
    end

    if arr[4] > 10 then
        for k in 0..4 loop
            print_integer(arr[k])
        end
    end

    for k in 4..0 reverse loop
        print_integer(arr[k])
    end

    for v in arr loop
        sum := sum + v
    end
    print_integer(sum)

    var n : integer = 10
    while n > 0 loop
        if n % 2 = 0 then
            print_integer(n)
        end
        n := n - 1
    end
end
