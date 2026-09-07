var n : integer = 6
var adj : array[6] array[6] boolean
var visited : array[6] boolean
var stack : array[6] integer
var top : integer = 0

routine push(v : integer) =
    stack[top] := v
    top := top + 1
end

routine pop() : integer =
    top := top - 1
    return stack[top]
end

routine is_empty() : boolean => top = 0

routine add_edge(u : integer, v : integer) =
    adj[u][v] := true
    adj[v][u] := true
end

routine dfs(start : integer) =
    push(start)

    while not is_empty() loop
        var v : integer = pop()

        if not visited[v] then
            visited[v] := true
            print_integer(v)

            var neighbor : integer = 0
            while neighbor < n loop
                if adj[v][neighbor] and not visited[neighbor] then
                    push(neighbor)
                end
                neighbor := neighbor + 1
            end
        end
    end
end

routine main() =
    add_edge(0, 1)
    add_edge(0, 2)
    add_edge(1, 3)
    add_edge(2, 4)
    add_edge(3, 5)
    add_edge(4, 5)

    dfs(0)
end
