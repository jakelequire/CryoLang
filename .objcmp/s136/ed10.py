p = "C:/Programming/apps/CryoLang/tests/started-compiling"
s = open(p, encoding="utf-8", newline="").read()
if not s.endswith("\n"):
    s += "\n"
s += ("generic_trait_implemented_twice_through_bound one type implementing a generic trait at two "
      "arguments, called through a bound naming one, is answered by the implementation whose head "
      "writes the bound's argument and the two methods link as two symbols, where the baseline left "
      "the call unbound (E0900 in each copy)\n")
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
