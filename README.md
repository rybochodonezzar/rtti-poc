
# rtti-poc
Proof of concept for RTTI system

**solution features**

* informations about types are stored in attrubutes which are [LiteralType](https://en.cppreference.com/w/cpp/named_req/LiteralType) classes

    Attributes include field desctiptors, class name descriptor and inheritance descriptors

* attributes are stored in List which is constexpr generic lambda wrapper around parameter pack
* attributes for type T are stored in RTTIinfo<T> which allows for attaching attributes to externally defined classes
* List can store any non-void LiteralType values, which allows for storing attributes that are not yet handled
* List of attributes can be converted to TypeInfo  which can be registered in TypeManager

    TypeInfo stores (handled) attributes along with constructor and serialization callback, etc.
* RTTIBase class enables reflection if a class virtually inherits it.

    Note that registering a type in TypeManager does not require RTTIBase inheritance

* **this solution uses no C macros**

**secondary stuff**

* Since TypeInfo uses std::type_index for type id instead of internally tracked int, types registered in TypeManager can be compared with unregistered types. For example if B : A, B::isTypeOf<A>() will return true even if A is not registered (as shown in PoC in main()). This will only work for direct inheritance.
* I didn't use derived<RTTIBase> since it's serialization is noop. I haven't tought about how to
  handle virtual inheritance in general case yet.
