# v4.0 Alpha 1 (unreleased)

## New features

* [CORE-5568](http://tracker.firebirdsql.org/browse/CORE-5568): SQL SECURITY feature  
  Reference(s): [/doc/sql.extentions/README.sql_security](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.sql_security.txt)  
  Contributor(s): Roman Simakov

* [CORE-5525](http://tracker.firebirdsql.org/browse/CORE-5525): Create high-precision floating point datatype named DECFLOAT  
  Reference(s): [/doc/sql.extentions/README.data_types](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.data_types)  
  Contributor(s): Alex Peshkoff

* [CORE-5488](http://tracker.firebirdsql.org/browse/CORE-5488): Timeouts for running SQL statements and idle connections  
  Reference(s): [/doc/README.session_idle_timeouts](https://github.com/FirebirdSQL/firebird/raw/master/doc/README.session_idle_timeouts), [/doc/README.statement_timeouts](https://github.com/FirebirdSQL/firebird/raw/master/doc/README.statement_timeouts)  
  Contributor(s): Vlad Khorsun

* [CORE-5463](http://tracker.firebirdsql.org/browse/CORE-5463): Support DEFAULT context value in INSERT, UPDATE, MERGE and UPDATE OR INSERT statements  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5449](http://tracker.firebirdsql.org/browse/CORE-5449): Support GENERATED ALWAYS identity columns and OVERRIDE clause  
  Reference(s): [doc/sql.extensions/README.identity_columns](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.identity_columns.txt)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5346](http://tracker.firebirdsql.org/browse/CORE-5346): Named windows  
  Reference(s): [doc/sql.extensions/README.window_functions](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.window_functions.md)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5343](http://tracker.firebirdsql.org/browse/CORE-5343): Allow particular DBA privileges to be transferred to regular users  
  Reference(s): [doc/sql.extensions/README.builtin_functions](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.builtin_functions.txt), [doc/sql.extensions/README.ddl](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.ddl.txt)  
  Contributor(s): Alex Peshkoff

* [CORE-3647](http://tracker.firebirdsql.org/browse/CORE-3647): Frames for window functions  
  Reference(s): [doc/sql.extensions/README.window_functions](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.window_functions.md)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-2762](http://tracker.firebirdsql.org/browse/CORE-2762): New built-in function to check whether some role is implicitly active  
  Reference(s): [doc/sql.extensions/README.cumulative_roles](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.cumulative_roles.txt)  
  Contributor(s): Roman Simakov

* [CORE-1815](http://tracker.firebirdsql.org/browse/CORE-1815): Ability to grant role to another role  
  Reference(s): [doc/sql.extensions/README.cumulative_roles](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.cumulative_roles.txt)  
  Contributor(s): Roman Simakov

* [CORE-1688](http://tracker.firebirdsql.org/browse/CORE-1688): More ANSI SQL:2003 window functions (PERCENK_RANK, CUME_DIST, NTILE)  
  Reference(s): [doc/sql.extensions/README.window_functions](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.window_functions.md)  
  Contributor(s): Hajime Nakagami, Adriano dos Santos Fernandes

* [CORE-751](http://tracker.firebirdsql.org/browse/CORE-751): Implicitly active roles (and their permissions summarized)  
  Contributor(s): Roman Simakov

## Improvements

* [CORE-5431](http://tracker.firebirdsql.org/browse/CORE-5431): Support for DROP IDENTITY clause  
  Reference(s): [doc/sql.extensions/README.identity_columns](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.identity_columns.txt)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5430](http://tracker.firebirdsql.org/browse/CORE-5430): Support for INCREMENT option in identity columns  
  Reference(s): [doc/sql.extensions/README.identity_columns](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.identity_columns.txt)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5380](http://tracker.firebirdsql.org/browse/CORE-5380): Allow subroutines to call others subroutines and themself recursively  
  Reference(s): [doc/sql.extensions/README.subroutines.txt](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.subroutines.txt)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5238](http://tracker.firebirdsql.org/browse/CORE-5238): Replace xinetd support with the native listener
  Contributor(s): Alex Peshkoff

* [CORE-5119](http://tracker.firebirdsql.org/browse/CORE-5119): Support autocommit mode in SET TRANSACTION statement  
  Contributor(s): Dmitry Yemanov

* [CORE-5064](http://tracker.firebirdsql.org/browse/CORE-5064): Add datatypes (VAR)BINARY(n) and BINARY VARYING(n) as aliases for (VAR)CHAR(n) CHARACTER SET OCTETS  
  Contributor(s): Dimitry Sibiryakov

* [CORE-4436](http://tracker.firebirdsql.org/browse/CORE-4436): Support for different hash algorithms in HASH system function  
  Reference(s): [doc/sql.extensions/README.builtin_functions.txt](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.builtin_functions.txt)  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-2557](http://tracker.firebirdsql.org/browse/CORE-2557): Grants on MON$ tables  
  Contributor(s): Alex Peshkoff

* [CORE-2216](http://tracker.firebirdsql.org/browse/CORE-2216): NBackup as online dump  
  Contributor(s): Roman Simakov, Vlad Khorsun

* [CORE-2192](http://tracker.firebirdsql.org/browse/CORE-2192): Extend maximum database page size to 32KB  
  Contributor(s): Dmitry Yemanov

* [CORE-2040](http://tracker.firebirdsql.org/browse/CORE-2040): Allow exception name and possibly exception text to be determined within a "WHEN ANY" error handling block  
  Reference(s): [doc/sql.extensions/README.context_variables](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.context_variables)  
  Contributor(s): Dmitry Yemanov

* [CORE-1132](http://tracker.firebirdsql.org/browse/CORE-1132): Exception context in PSQL exception handlers  
  Reference(s): [doc/sql.extensions/README.context_variables](https://github.com/FirebirdSQL/firebird/raw/master/doc/sql.extensions/README.context_variables)  
  Contributor(s): Dmitry Yemanov

* [CORE-749](http://tracker.firebirdsql.org/browse/CORE-749): Increase maximum length of object names to 63 characters  
  Contributor(s): Adriano dos Santos Fernandes

## Bugfixes

* [CORE-5545](http://tracker.firebirdsql.org/browse/CORE-5545): Wrong syntax with CREATE TRIGGER ... ON <table> used with POSITION  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5454](http://tracker.firebirdsql.org/browse/CORE-5454): INSERT into updatable view without explicit field list failed  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5408](http://tracker.firebirdsql.org/browse/CORE-5408): Result of boolean expression can not be concatenated with string literal  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5404](http://tracker.firebirdsql.org/browse/CORE-5404): Inconsistent column/line references when PSQL definitions return errors  
  Contributor(s): Adriano dos Santos Fernandes

* [CORE-5237](http://tracker.firebirdsql.org/browse/CORE-5237): Invalid handling of dot (.) and asterisk (*) in config file name and path for include clause  
  Contributor(s): Dimitry Sibiryakov

* [CORE-5223](http://tracker.firebirdsql.org/browse/CORE-5223): Double dots are prohibited in file names if access is restricted to a list of directories  
  Contributor(s): Dimitry Sibiryakov

* [CORE-5141](http://tracker.firebirdsql.org/browse/CORE-5141): Field definition allows several NOT NULL clauses  
  Contributor(s): Dimitry Sibiryakov

* [CORE-4985](http://tracker.firebirdsql.org/browse/CORE-4985): Non-privileged user can implicitly count records in a restricted table  
  Contributor(s): Dmitry Yemanov

* [CORE-4701](http://tracker.firebirdsql.org/browse/CORE-4701): Index and blob garbage collection don't take into accout data in undo log  
  Contributor(s): Dimitry Sibiryakov

* [CORE-4483](http://tracker.firebirdsql.org/browse/CORE-4483): Changed data not visible in WHEN-section if exception occured inside SP that has been called from this code  
  Contributor(s): Dimitry Sibiryakov

* [CORE-4424](http://tracker.firebirdsql.org/browse/CORE-4424): Rollback to wrong savepoint if several exception handlers on the same level are executed  
  Contributor(s): Dimitry Sibiryakov

