function WALL(ducq, msg)
	for client, route in ducq:clients() do
		client:send("WALL *\n" .. msg.payload)
	end
	return 0
end
