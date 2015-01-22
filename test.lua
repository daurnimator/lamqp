local amqp = require "amqp"
print("VERSION=",amqp.version_number())
do
	local c = amqp.new_connection()
	assert(c:get_sockfd() == nil)
	assert(c:get_socket() == nil)
	c:close()
end
collectgarbage()
do
	local c = amqp.new_connection()
	local sock = assert(c:tcp_socket_new())
	assert(sock == c:get_socket(), "get_socket did not return same socket as we created")
	assert(sock:open_noblock("127.0.0.1", 5672))
	assert(c:get_sockfd())
end
do
	local c = amqp.new_connection()
	local sock = assert(c:ssl_socket_new())
	assert(sock == c:get_socket(), "get_socket did not return same socket as we created")
	assert(sock:open_noblock("127.0.0.1", 5672))
	assert(c:get_sockfd())
end
