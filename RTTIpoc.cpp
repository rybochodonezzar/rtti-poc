#include <iostream>
#include <list>
#include <string>
#include <type_traits>
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <typeinfo>
#include <unordered_map>
#include <typeindex>
#include <stdexcept>
#include <algorithm>
using namespace std;

/* generic functions */

constexpr auto List = [](auto ...xs) {
    return [=](auto access) { return access(xs...); };
};

constexpr auto fmap = [](auto func) {
    return [func] (auto alist) {
        return alist([func](auto... xs) { return List(func(xs)...); });
    };
};

template <typename F, typename X, typename ... XS>
void for_pack(F f, X x, XS... xs)
{
    f(x);
    for_pack(f, xs...);
}

template <typename F, typename X>
void for_pack(F f, X x) { f(x); }

constexpr auto for_all = [](auto list, auto f) {
    list([f](auto... xs){ for_pack(f, xs...); });
};

template <typename T>
auto runByBase = [](auto x, auto f)
{
    x([&](auto ... xs) {
	auto fun = [&](auto y) {
	    if constexpr (is_base_of<T, decltype(y)>::value)
		f(y);
	};
	for_pack(fun, xs...);
    });
};

/* attribute defintions */

template <typename T> struct RTTIinfo;
template <typename T> struct FieldTypeStuff; //need better name

struct isField {
    const char* name;
    virtual string toString(void* source) = 0;
    const char* typeName;
    constexpr isField(const char* name, const char* typeName) : name{name}, typeName{typeName} {}
};


template <typename T, typename C>
struct Field : public isField {
    using type = T;
    T C::*fieldptr;
    constexpr Field(T C::*fieldptr, const char* name) : isField{name, FieldTypeStuff<T>::name}, fieldptr{fieldptr} {}
    string toString(void* source) { return FieldTypeStuff<T>::toString((C*)source->*fieldptr); }
};

template <> struct FieldTypeStuff<int> {
    constexpr static const char* name{"int"};
    static string toString(int source){ return to_string(source); }
};

template <> struct FieldTypeStuff<string> {
    constexpr static const char* name{"string"};
    static string toString(string& source) { return string(source); }
};
    
template <typename Type, typename Class> constexpr Field<Type, Class> field(Type Class::*ptr, const char* name) { return Field<Type, Class>{ptr, name}; }

struct classname { const char* name; };

struct isDerived {};

template <typename T>
struct derived : public isDerived {
    using type = T;
    constexpr static auto info = RTTIinfo<T>::info;
    constexpr derived() : isDerived{} {}
};

/* static serialization */

auto getClassName = [](auto lst)
{
    const char* name = NULL;
    lst([&](auto ... xs) {
	auto fun = [&](auto x) {
	    if constexpr (is_same<classname, decltype(x)>::value)
		name = x.name;
	};
	for_pack(fun, xs...);
    });
    return name;
};

template <typename T> const char* getTypeName() { return getClassName(RTTIinfo<T>::info); }
template <> const char* getTypeName<int>() { return "int"; }
template <> const char* getTypeName<string>() { return "string"; }

template <typename T> void serialize(T& src)
{
    const char * name = getTypeName<T>();
    cout << "struct " << name;
    bool isColonPrinted = false;
    runByBase<isDerived>(RTTIinfo<T>::info, [&](auto base){
	if(isColonPrinted)
	    cout << ", " << getClassName(base.info);
	else {
	    cout << " : " << getClassName(base.info);
	    isColonPrinted = true;
	}
    });
    cout << "\n{\n";
    runByBase<isDerived>(RTTIinfo<T>::info, [&](auto base) {
	const char* basename = getClassName(base.info);
	runByBase<isField>(base.info, [&](auto field) {
	    cout << "    " << field.typeName << " " << basename << "::" << field.name << " = " << field.toString(&src) << "\n";
	});
    });
    runByBase<isField>(RTTIinfo<T>::info, [&](auto field) {
	cout << "    " << field.typeName << " " << name << "::" << field.name << " = " << field.toString(&src) << "\n";
    });
    cout << "};\n";
}

/* TypeManager */

struct TypeInfo {
    const char* name;
    type_index id;
    vector<type_index> baseIds;
    vector<unique_ptr<isField>> fields;
    function<void*()> construct;
    function<void(void*)> serialize;
    TypeInfo() :
	name{nullptr}
	, id{typeid(void)}
	, baseIds{}
	, fields{}
	, construct{nullptr}
	, serialize{nullptr}
	{}
    template <typename T> void init()
    {
	for_all(RTTIinfo<T>::info, [this](auto x){
	    if constexpr (is_same<classname, decltype(x)>::value)
		name = x.name;
	    if constexpr (is_base_of<isDerived, decltype(x)>::value) {
		using type = typename decltype(x)::type;
		baseIds.emplace_back(type_index{typeid(type)});
	    }
	    if constexpr (is_base_of<isField, decltype(x)>::value) {
		using FieldT = decltype(x);
		fields.emplace_back(unique_ptr<isField>{new FieldT{x}});
	    }
	});
	id = type_index{typeid(T)};
	construct = [](){ return new T; };
	serialize = [](void* p){ ::serialize<T>(*(T*)p); };
    }
};

struct TypeManager {
    static TypeManager& get()
    {
	static TypeManager instance{};
	return instance;
    }
    template <typename T> TypeInfo& registerOrGetType()
    {
        if(byId.count(typeid(T)))
	    return *byId.at(typeid(T));
	auto p = make_shared<TypeInfo>();
	p->init<T>();
	if(byName.count(p->name)) {
	    string s = "different class with name ";
	    s += p->name;
	    s += " is alredy registered";
	    throw runtime_error(s);
	}
	cout << "register type " << p->name << " hash " << p->id.hash_code() << endl;
	byName.insert_or_assign(p->name, p);
	byId.insert_or_assign(p->id, p);
	return *p;
    }
    unordered_map<string, shared_ptr<TypeInfo>> byName;
    unordered_map<type_index, shared_ptr<TypeInfo>> byId;
    private:
    TypeManager() = default;
    TypeManager(const TypeManager& rhs) = delete;
    TypeManager& operator=(const TypeManager& rhs) = delete;
};

/* reflection */

template <typename T> bool isDerivedOrSame(const TypeInfo& ti)
{
    return type_index{typeid(T)} == ti.id
	|| any_of(ti.baseIds.begin(), ti.baseIds.end(), [&](auto& idx) {
	    auto& tm = TypeManager::get();
	    return idx == type_index{typeid(T)} // this condition may be checked twice, but this is just PoC code
		|| tm.byId.count(idx) && isDerivedOrSame<T>(*tm.byId.at(idx));
	});
}

struct RTTIBase {
    const TypeInfo& typeInfo;
    template <typename T> bool isOfType() { return isDerivedOrSame<T>(typeInfo); }
};

/* test classes */

struct A : virtual RTTIBase {
    A() : RTTIBase{TypeManager::get().registerOrGetType<A>()} {}
    int intVal;
    string strVal;
};

template <> struct RTTIinfo<A> {
    constexpr static auto info = List(
	classname{"A"},
	field(&A::intVal, "intVal"),
	"kind of useless string",
	field(&A::strVal, "strVal")
    );
};

struct B : virtual RTTIBase, A {
    B() : RTTIBase{TypeManager::get().registerOrGetType<B>()}, A() {}
    int intVal2;
};

template <> struct RTTIinfo<B> {
    constexpr static auto info = List(
	classname{"B"},
	derived<A>{},
	field(&B::intVal2, "intVal2")
    );
};

int main()
{
    auto& tm = TypeManager::get();
    //tm.registerOrGetType<A>();
    B b;
    b.intVal = 7;
    b.intVal2 = 666;
    b.strVal = "asdasfasfdas";
    serialize(b);
    cout << b.isOfType<B>() << endl;
    cout << b.isOfType<A>() << endl;
    cout << b.isOfType<int>() << endl;
    return 0;
}
