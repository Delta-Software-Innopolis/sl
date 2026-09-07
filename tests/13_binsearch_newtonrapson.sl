routine binary_sqrt(
    target: real,
    left: real,
    right: real,
    steps: integer
): real =
    var mid: real

    for i in 1 .. steps loop
        mid := (left + right) / 2.0

        if (mid * mid) > target then
            right := mid
        else
            left := mid
        end
    end

    return (left + right) / 2.0
end

routine newton_sqrt(target: real, guess: real, steps: integer): real =
    for i in 1 .. steps loop
        guess := (guess + (target / guess)) / 2.0
    end

    return guess
end

routine hybrid_sqrt(target: real): real =
    var first_guess: real
    var result: real

    first_guess := binary_sqrt(target, 1.0, target, 8)
    result := newton_sqrt(target, first_guess, 4)

    return result
end

routine main() =
    var result: real
    var correct: boolean

    result := hybrid_sqrt(2.0)

    correct := (result > 1.414) and (result < 1.415)

    print_real(result)
    print_boolean(correct)
end
