An abandoned attempt to write lua bindings to [rabbitmq-c](https://github.com/alanxz/rabbitmq-c)

I realised that the most functions (e.g. `amqp_login`) were blocking,
and no async variants were available.

I thought I should commit it as it could be a starting point for someone else.
