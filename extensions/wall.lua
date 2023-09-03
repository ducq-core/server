function WALL(requestor, msg)
	reactor:loop(function (ducq, route)
		ducq:send("WALL *\n" .. msg.payload)
		return reactor.continue
	end)
	return 0
end
