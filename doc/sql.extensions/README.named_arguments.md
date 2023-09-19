# Named arguments for function and procedure calling (FB 6.0)

Named arguments allows you to specify function and procedure arguments by their names, rather than only by their positions.

It is especially useful when the routine have a lot of parameters and you want to specify them in arbitrary order or not specify some of them who have default values.

As the positional syntax, all arguments without default values are required to be present in the call.

It's currently not possible to mix positional and named arguments in the same call.

## Syntax

```
<function call> ::=
    [<package name> .] <function_name>( [<arguments>] )

<procedure selection> ::=
    [<package name> .] <procedure_name> [( <arguments> )]

<execute procedure> ::=
    EXECUTE PROCEDURE [<package name> .] <procedure name>
    [{ (<arguments>) | <arguments> }]
    [RETURNING_VALUES ...]

<arguments> ::=
    <named arguments>
    <positional arguments>

<named arguments> ::=
    <named argument> [ {, <named argument>}... ]

<named argument> ::=
    <argument name> => <value>
```

## Examples

```
select function_name(parameter2 => 'Two', parameter1 => 1)
  from rdb$database
```

```
execute procedure insert_customer(
  last_name => 'SCHUMACHER',
  first_name => 'MICHAEL')
```

```
select *
  from get_customers(city_id => 10, last_name => 'SCHUMACHER')
```
