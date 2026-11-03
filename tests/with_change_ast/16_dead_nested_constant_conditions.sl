routine main() =
    var n : integer = 0
    while n < 3 loop
        if false then
            print_integer(100)
        end
        if true then
            n := n + 1
        end
        while false loop
            print_integer(200)
        end
    end
    print_integer(n)
end
