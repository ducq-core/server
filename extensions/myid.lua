function myid(ducq, msg)
	for client, route in ducq:clients() do
		if client == ducq then
			ducq:send( ducq:id() .. ' ' .. route )
			break
		end
	end
	return 0
end
