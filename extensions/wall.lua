commands.WALL = {
	doc = "send a PING to all clients",
	exec = function (ducq, msg)
		local payload = msg.payload
		if payload == nil or payload == '' then
			payload = "ping"
		end

		for client, route in ducq:clients() do
			client:send("PING _\n" .. payload)
		end
		return 0
	end
}
