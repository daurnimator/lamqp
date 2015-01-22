#include "lua.h"
#include "lauxlib.h"
#include "amqp.h"
#include "amqp_tcp_socket.h"
#include "amqp_ssl_socket.h"

#include <sys/time.h> /* struct timeval */
#include <math.h> /* HUGE_VAL, floor */

#define LAMQP_NO_MEM "no memory"
static const void* socket_weak_key;

static int lamqp_version_number(lua_State *L) {
	lua_pushinteger(L, amqp_version_number());
	return 1;
}

static int lamqp_version(lua_State *L) {
	lua_pushstring(L, amqp_version());
	return 1;
}

static int lamqp_new_connection(lua_State *L) {
	amqp_connection_state_t *conn = lua_newuserdata(L, sizeof(amqp_connection_state_t));
	luaL_setmetatable(L, "amqp_connection_state_t");
	*conn = amqp_new_connection();
	if (*conn == NULL) {
		return luaL_error(L, LAMQP_NO_MEM);
	}
	return 1;
}

static amqp_connection_state_t lamqp_check_connection(lua_State *L, int arg) {
	amqp_connection_state_t *conn = luaL_checkudata(L, arg, "amqp_connection_state_t");
	luaL_argcheck(L, *conn != NULL, arg, "invalid connection");
	return *conn;
}

static amqp_socket_t* lamqp_check_socket(lua_State *L, int arg) {
	amqp_socket_t **sock = luaL_checkudata(L, arg, "amqp_socket_t*");
	luaL_argcheck(L, *sock != NULL, arg, "invalid socket");
	return *sock;
}

static void lamqp_push_socket(lua_State *L, amqp_socket_t *sock) {
	/* check if socket is already in cache */
	lua_rawgetp(L, LUA_REGISTRYINDEX, socket_weak_key);
	lua_rawgetp(L, -1, sock);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1); /* pop the nil */
		*((amqp_socket_t**)lua_newuserdata(L, sizeof(amqp_socket_t*))) = sock;
		luaL_setmetatable(L, "amqp_socket_t*");
		/* store it in the registry so any get_socket calls return the same object */
		lua_pushvalue(L, -1);
		lua_rawsetp(L, -3, sock);
	}
	/* remove socket cache from stack */
	lua_remove(L, lua_gettop(L)-1);
}

static int lamqp_return_amqp_status_enum(lua_State *L, amqp_status_enum r) {
	if (r == AMQP_STATUS_OK) {
		lua_pushboolean(L, 1);
		return 1;
	} else {
		lua_pushnil(L);
		lua_pushstring(L, amqp_error_string2(r));
		lua_pushinteger(L, r);
		return 3;
	}
}

static int lamqp_return_amqp_rpc_reply_t(lua_State *L, amqp_rpc_reply_t r) {
	switch(r.reply_type) {
	case AMQP_RESPONSE_NORMAL:
		lua_pushboolean(L, 1);
		return 1;
	case AMQP_RESPONSE_SERVER_EXCEPTION:
		lua_pushnil(L);
		lua_pushliteral(L, "server exception");
		lua_pushinteger(L, r.reply.id);
		return 3;
	case AMQP_RESPONSE_LIBRARY_EXCEPTION:
		return lamqp_return_amqp_status_enum(L, r.library_error);
	default:
		lua_pushnil(L);
		return 1;
	}
}

static int lamqp_destroy_connection(lua_State *L) {
	amqp_connection_state_t conn = lamqp_check_connection(L, 1);
	amqp_status_enum r = amqp_destroy_connection(conn);
	*(void**)lua_touserdata(L, 1) = NULL; /* invalidate connection */
	return lamqp_return_amqp_status_enum(L, r);
}

static int lamqp_get_socket(lua_State *L) {
	amqp_connection_state_t conn = lamqp_check_connection(L, 1);
	amqp_socket_t *sock = amqp_get_socket(conn);
	if (NULL == sock) {
		lua_pushnil(L);
		return 1;
	}
	lamqp_push_socket(L, sock);
	return 1;
}

static int lamqp_get_sockfd(lua_State *L) {
	amqp_connection_state_t conn = lamqp_check_connection(L, 1);
	int fd = amqp_get_sockfd(conn);
	if (-1 == fd) {
		lua_pushnil(L);
	} else {
		lua_pushinteger(L, fd);
	}
	return 1;
}

static int lamqp_ssl_socket_new(lua_State *L) {
	amqp_connection_state_t conn = lamqp_check_connection(L, 1);
	amqp_socket_t **sock = lua_newuserdata(L, sizeof(amqp_socket_t*));
	*sock = amqp_ssl_socket_new(conn);
	if (NULL == *sock) {
		lua_pushnil(L);
		return 1;
	}
	lamqp_push_socket(L, *sock);
	return 1;
}

static int lamqp_tcp_socket_new(lua_State *L) {
	amqp_connection_state_t conn = lamqp_check_connection(L, 1);
	amqp_socket_t **sock = lua_newuserdata(L, sizeof(amqp_socket_t*));
	*sock = amqp_tcp_socket_new(conn);
	if (NULL == *sock) {
		lua_pushnil(L);
		return 1;
	}
	lamqp_push_socket(L, *sock);
	return 1;
}

static int lamqp_connection_close(lua_State *L) {
	amqp_connection_state_t conn = lamqp_check_connection(L, 1);
	int code = luaL_optinteger(L, 2, AMQP_REPLY_SUCCESS);
	amqp_rpc_reply_t r;
	if (amqp_get_socket(conn) == NULL) {
		/* avoid segfault: https://github.com/alanxz/rabbitmq-c/issues/237#issuecomment-71058851 */
		lua_pushboolean(L, 0);
		return 1;
	}
	r = amqp_connection_close(conn, code);
	return lamqp_return_amqp_rpc_reply_t(L, r);
}

static int lamqp_socket_open_noblock(lua_State *L) {
	amqp_socket_t *sock = lamqp_check_socket(L, 1);
	const char *host = luaL_checkstring(L, 2);
	int port = luaL_checkinteger(L, 3);
	double timeout = luaL_optnumber(L, 4, HUGE_VAL);
	struct timeval tv_timeout, *tv_timeoutp;
	int r;
	if (timeout == HUGE_VAL) {
		tv_timeoutp = NULL;
	} else {
		tv_timeoutp = &tv_timeout;
		tv_timeout.tv_sec = floor(timeout);
		tv_timeout.tv_usec = floor((timeout - floor(timeout)) * 1e6);
	}
	r = amqp_socket_open_noblock(sock, host, port, tv_timeoutp);
	return lamqp_return_amqp_status_enum(L, r);
}

static const luaL_Reg lamqp_lib[] = {
	{"version_number", lamqp_version_number},
	{"version", lamqp_version},
	{"new_connection", lamqp_new_connection},
	{NULL, NULL}
};

static const luaL_Reg lamqp_connection_methods[] = {
	{"tcp_socket_new", lamqp_tcp_socket_new},
	{"ssl_socket_new", lamqp_ssl_socket_new},
	{"get_socket", lamqp_get_socket},
	{"get_sockfd", lamqp_get_sockfd},
	{"close", lamqp_connection_close},
	{NULL, NULL}
};

static const luaL_Reg lamqp_socket_methods[] = {
	{"open_noblock", lamqp_socket_open_noblock},
	{NULL, NULL}
};

int luaopen_amqp(lua_State *L) {
	luaL_newlib(L, lamqp_lib);
	if (luaL_newmetatable(L, "amqp_connection_state_t") == 1) {
		lua_pushcfunction(L, lamqp_destroy_connection);
		lua_setfield(L, -2, "__gc");
		luaL_newlib(L, lamqp_connection_methods);
		lua_setfield(L, -2, "__index");
	}
	if (luaL_newmetatable(L, "amqp_socket_t*") == 1) {
		luaL_newlib(L, lamqp_socket_methods);
		lua_setfield(L, -2, "__index");
	}
	/* create weak map for sockets in registry */
	lua_newtable(L);
	lua_createtable(L, 0, 1);
	lua_pushliteral(L, "v");
	lua_setfield(L, -2, "__mode");
	lua_setmetatable(L, -2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, socket_weak_key);

	lua_pop(L, 2); /* pop metatables */
	return 1;
}
