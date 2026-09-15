// token_demo.sl — exercises every token type the lexer knows about
type Point = record
    var x: integer;
    var y: real
end

routine classify(n: integer): boolean =>
    n >= 0 and n /= 0

routine main() =
    var flag: boolean = true
    var arr: array[5] integer
    var p: Point
    var i: integer

    if flag and not false then
        p.x := 1
        p.y := 2.5
    else
        p.x := -1
    end

    for i in 0..4 loop
        arr[i] := i * i + 1
    end

    for i in 4..0 reverse loop
        arr[i] := arr[i] - 1 % 2
    end

    while i < 10 or i <= 20 loop
        i := i + 1
        arr[i] := arr[i] / 2
        foo(i, arr[i])
    end

    if i > 100 xor flag then
        return true
    end

    return false
end
