routine g(x : integer) : integer =
    return x * 2
end

routine main() =
    print_integer(g(3) + 0)
    print_integer(1 * g(4))
end
