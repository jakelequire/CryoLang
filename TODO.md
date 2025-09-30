

- the string primitive is not calling `.append()` method properly but `.length()` works.
```cryo
    // const example_str: string = "Cryo";
    // example_str.append("Lang");
```


- Change LLVM backend to handle the Cryo type system rather than string parsing for type information. This is a larger achitectural change and would need to be done carefully.
```c++
// For generic types, extract the base type name for field/method lookup
std::string lookup_type = object_type;
size_t generic_pos = object_type.find('<');
if (generic_pos != std::string::npos)
{
    lookup_type = object_type.substr(0, generic_pos);
}
```