package = "lamqp"
version = "scm-0"
source = {
   url = "git://github.com/daurnimator/lamqp";
}
description = {
   summary = "A Lua Binding for librabbitmq-c";
   homepage = "http://github.com/daurnimator/lamqp";
   license = "MIT/X11";
}
dependencies = {
   "lua >= 5.1";
}
external_dependencies = {
   RABBITMQ = {
      header = "amqp.h";
      library = "rabbitmq";
   };
}
build = {
   type = "builtin";
   modules = {
      amqp = {
         sources = "lamqp.c";
         incdirs = { "$(RABBITMQ_INCDIR)" };
         libdirs = { "$(RABBITMQ_LIBDIR)" };
         libraries = { "rabbitmq" };
      };
   };
}
