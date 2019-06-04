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
#include <any>
#include <utility>
using namespace std;

struct classname;

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

using AttrMap = unordered_map<type_index, any>;

template <typename T> struct SimpleAttribute {
    using storage_type = vector<T>;
    static void add(AttrMap& m, T x)
    {
        auto i = m.find(typeid(T));
        if(i == m.end())
            i = m.emplace(typeid(T), storage_type{}).first;
        any_cast<storage_type>(&i->second)->emplace_back(x);
    }
    static const storage_type& get(const AttrMap& m)
    {
        return any_cast<const storage_type&>(m.at(typeid(T)));
    }
};

template <typename T> struct DerivedAttribute {
    using storage_type = vector<shared_ptr<T>>;
    template <typename TT> static void add(AttrMap& m, TT x)
    {
        static_assert(is_base_of<T,TT>::value, "invalid type");
        auto i = m.find(typeid(T));
        if(i == m.end())
            i = m.emplace(typeid(T), storage_type{}).first;
        any_cast<storage_type>(&i->second)->emplace_back(shared_ptr<T>{
            new TT{x},
            [](T* p){ delete static_cast<TT*>(p); } //TODO: check if dynamic cast is not needed
        });
    }
    static const storage_type& get(const AttrMap& m)
    {
        return any_cast<storage_type>(m.at(typeid(T)));
    }
};

template <typename T> struct UniqueAttribute {
    using storage_type = T;
    static void add(AttrMap& m, T x)
    {
        if(!m.try_emplace(typeid(T), x).second)
            throw runtime_error("inserting second UniqueAttribute");
    }
    static const storage_type& get(const AttrMap& m)
    {
        return any_cast<storage_type>(m.at(typeid(T)));
    }
};

template <typename T> struct AttrType {
    using type = SimpleAttribute<T>;
};


struct isField {
    const char* name;
    virtual string toString(void* source) = 0;
    const char* typeName;
    constexpr isField(const char* name, const char* typeName) : name{name}, typeName{typeName} {}
};

template <> struct AttrType<isField> { using type = DerivedAttribute<isField>; };

template <typename T, typename C>
struct Field : public isField {
    using type = T;
    using attr_type = DerivedAttribute<isField>;
    T C::*fieldptr;
    constexpr Field(T C::*fieldptr, const char* name) : isField{name, FieldTypeStuff<T>::name}, fieldptr{fieldptr} {}
    string toString(void* source) { return FieldTypeStuff<T>::toString((C*)source->*fieldptr); }
};

template <typename T, typename C> struct AttrType<Field<T,C>> {
    using type = DerivedAttribute<isField>;
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
template <> struct AttrType<classname> { using type = UniqueAttribute<classname>; };

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
    using deleterT = function<void(isField*)>;
    vector<unique_ptr<isField, deleterT>> fields;
    AttrMap attrs;
    function<void*()> construct;
    function<void(void*)> serialize;
    TypeInfo() :
	name{nullptr}
	, id{typeid(void)}
	, baseIds{}
	, fields{}
        , attrs{}
	, construct{nullptr}
	, serialize{nullptr}
	{}
    template <typename T> void init()
    {
	for_all(RTTIinfo<T>::info, [this](auto x){
            using xT = decltype(x);
	    if constexpr (is_same<classname, xT>::value)
		name = x.name;
	    if constexpr (is_base_of<isDerived, xT>::value) {
		using type = typename xT::type;
		baseIds.emplace_back(type_index{typeid(type)});
	    }
	    if constexpr (is_base_of<isField, deleterT>::value) {
		using FieldT = xT;
                auto deleter = [](isField* p){ delete (static_cast<xT*>(p)); };
		fields.emplace_back(unique_ptr<isField, deleterT>(new FieldT{x}, deleter));
	    }
            AttrType<xT>::type::add(attrs, x);

	});
	id = type_index{typeid(T)};
	construct = [](){ return new T; };
	serialize = [](void* p){ ::serialize<T>(*(T*)p); };
    }
    template <typename T> bool hasAttr()
    {
        return attrs.count(typeid(T));
    }
    bool hasAttr(type_index i)
    {
        return attrs.count(i);
    }
    template <typename T>
    typename AttrType<T>::type::storage_type* get()
    {
        auto i = attrs.find(typeid(T));
        return i != attrs.end() ? any_cast<typename AttrType<T>::type::storage_type>(&i->second) : nullptr;
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
		|| (tm.byId.count(idx) && isDerivedOrSame<T>(*tm.byId.at(idx)));
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
	//derived<A>{},
	field(&B::intVal2, "intVal2")
    );
};

struct C : virtual RTTIBase {
    C() : RTTIBase{TypeManager::get().registerOrGetType<C>()} {}
};
template <> struct RTTIinfo<C> {
    constexpr static auto info = List(
	classname{"C"}
    );
};

int main()
{
    auto& tm = TypeManager::get();
    auto x = tm.registerOrGetType<C>().get<classname>();
    cout << x->name << endl;
    auto& y = tm.registerOrGetType<A>();
   for(auto& z : *y.get<isField>())
        cout << z->name << endl;
    //B b;
    //b.intVal = 7;
    //b.intVal2 = 666;
    //b.strVal = "asdasfasfdas";
    //serialize(b);
    //cout << b.isOfType<B>() << endl;
    //cout << b.isOfType<A>() << endl;
    //cout << b.isOfType<int>() << endl;
    return 0;
}
