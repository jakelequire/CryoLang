p = 'compiler/src/compiler/mono/call_specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """        mut method_param_names: SymbolStr[] = [];
        for (mut i: i64 = 0; i < orig_method.func.generic_params.length; i++) {
            const gp: GenericParamNode* = orig_method.func.generic_params[i];
            if (gp == null) { return false; }
            method_param_names.push(gp.name);
        }
        if (method_param_names.length != method_binds.length) { return false; }

"""
new = """        for (mut i: i64 = 0; i < orig_method.func.generic_params.length; i++) {
            if (orig_method.func.generic_params[i] == null) { return false; }
        }
        if (orig_method.func.generic_params.length != method_binds.length) { return false; }

"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
