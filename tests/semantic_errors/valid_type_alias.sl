type BaseNumber = integer
type Number = BaseNumber
type Counter = Number

var counter: Counter = 0

routine main() =
    counter := counter + 1
    print_integer(counter)
end
