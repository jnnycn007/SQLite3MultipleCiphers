# SQLite3MultipleCiphers

The project **SQLite3 Multiple Ciphers** implements an encryption extension for [SQLite](https://www.sqlite.org) with support for multiple ciphers. In the past the encryption extension was bundled with the project [wxSQLite3](https://github.com/utelle/wxsqlite3), which provides a thin SQLite3 database wrapper for [wxWidgets](https://www.wxwidgets.org/).

In the course of time several developers had asked for a stand-alone version of the _wxSQLite3 encryption extension_. Originally it was planned to undertake the separation process already in 2019, but due to personal matters it had to be postponed for several months. However, maybe that wasn't that bad after all, because there were changes to the public SQLite code on Feb 7, 2020: [“Simplify the code by removing the unsupported and undocumented SQLITE_HAS_CODEC compile-time option”](https://www.sqlite.org/src/timeline?c=5a877221ce90e752). These changes took effect with the release of SQLite version 3.32.0 on May 22, 2020. As a consequence, all SQLite encryption extensions out there will not be able to easily support SQLite version 3.32.0 and later.

In late February 2020 work started on a new implementation of a SQLite encryption extension that will be able to support SQLite 3.32.0 and later. The new approach is based on [SQLite's VFS feature](https://www.sqlite.org/vfs.html). This approach has its pros and cons. On the one hand, the code is less closely coupled with SQLite itself; on the other hand, access to SQLite's internal data structures is more complex.

The code was mainly developed under Windows, but was tested under Linux as well. At the moment no major issues are known.

## Version information

* 2.2.3 - *July 2025*
  - Based on SQLite version 3.50.3

For further version information please consult the [CHANGELOG](CHANGELOG.md).

## How to participate

**Help in testing and discussing further development will be _highly_ appreciated**. Please use the [issue tracker](https://github.com/utelle/SQLite3MultipleCiphers/issues) to give feedback, report problems, or to discuss ideas.

## Documentation

Documentation of the currently supported cipher schemes and the C and SQL interfaces is provided on the [SQLite3 Multiple Ciphers website](https://utelle.github.io/SQLite3MultipleCiphers/).

Documentation on how to build the extension can be found on the page [SQLite3 Multiple Ciphers Installation](https://utelle.github.io/SQLite3MultipleCiphers/docs/installation/install_overview/).

## Supporters

[![JetBrains logo.](https://resources.jetbrains.com/storage/products/company/brand/logos/jetbrains.svg)](https://jb.gg/OpenSourceSupport) supports this project (and directly dependent projects like [wxSQLite3](https://github.com/utelle/wxsqlite3), [apsw-sqlite3mc](https://github.com/utelle/apsw-sqlite3mc), and [SQLite3MultipleCiphers-NuGet](https://github.com/utelle/SQLite3MultipleCiphers-NuGet)) by providing free [CLion](https://www.jetbrains.com/clion/) licenses. This helps to better maintain the project for different platforms.

## Support further development

[![Donate for SQLite3 Multiple Ciphers via PayPal](https://www.paypalobjects.com/de_DE/i/btn/x-click-but04.gif)](https://www.paypal.com/donate/?hosted_button_id=3JC9PMMH8X7UN) If you find **SQLite3 Multiple Ciphers** useful and would like to support the further development of this project, you may make a **donation** by clicking on the button on the left.
