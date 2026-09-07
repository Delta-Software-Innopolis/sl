type Point = record
    var x: integer
    var y: integer
end

type Circle = record
    var center: Point
    var radius: real
end

routine print_point(p: Point) =
    print_integer(p.x, p.y)
end

routine move_point(p: Point, dx: integer, dy: integer) =
    p.x := p.x + dx
    p.y := p.y + dy
end

routine distance_squared(p1: Point, p2: Point): integer =
    var dx: integer = p1.x - p2.x
    var dy: integer = p1.y - p2.y
    return dx * dx + dy * dy
end

routine main() =
    var p1: Point
    p1.x := 1
    p1.y := 2
    print_point(p1)

    move_point(p1, 10, 20)
    print_point(p1)

    var p2: Point = p1
    p2.x := 999
    print_point(p1)

    var p3: Point
    p3.x := 0
    p3.y := 0
    print_integer(distance_squared(p1, p3))

    var c: Circle
    c.center.x := 0
    c.center.y := 0
    c.radius := 5.5
    print_integer(c.center.x, c.center.y)
    print_real(c.radius)

    var points: array[3] Point
    for i in 0..2 loop
        points[i].x := i
        points[i].y := i * i
    end
    for i in 0..2 loop
        print_point(points[i])
    end
end
