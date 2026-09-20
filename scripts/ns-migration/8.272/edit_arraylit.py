def edit(p, reps):
    s = open(p, encoding='utf-8', newline='').read()
    for old, new, count in reps:
        assert s.count(old) == count, (p, old[:70], s.count(old))
        s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print("ok", p)

edit('compiler/src/compiler/AST/dumper.cryo', [
("""        this.print_location(node.span);
        if (node.element_type != "") {
            fmt::printf(" \\033[1;31m'%s'\\033[0m", node.element_type);
        }
        fmt::printf("\\n");

        for (mut i: int = 0; i < node.elements.length; i++) {
""", """        this.print_location(node.span);
        fmt::printf("\\n");

        for (mut i: int = 0; i < node.elements.length; i++) {
""", 1)])

edit('compiler/src/compiler/AST/cloner.cryo', [
("""        c.element_type = node.element_type;
        c.repeat_count = node.repeat_count;
""", """        c.repeat_count = node.repeat_count;
""", 1)])

edit('compiler/src/compiler/AST/expression.cryo', [
("""    elements:          ExpressionNode*[];
    element_type:      string;
    repeat_count:      i64;
    repeat_count_expr: ExpressionNode*;

    ArrayLiteralNode(span: SourceSpan)
        : ExpressionNode(NodeKind::ArrayLiteral, span) {
        this.elements          = [];
        this.element_type      = "";
        this.repeat_count      = 0;
""", """    elements:          ExpressionNode*[];
    repeat_count:      i64;
    repeat_count_expr: ExpressionNode*;

    ArrayLiteralNode(span: SourceSpan)
        : ExpressionNode(NodeKind::ArrayLiteral, span) {
        this.elements          = [];
        this.repeat_count      = 0;
""", 1),
("""    set_element_type(t: string) -> void {
        this.element_type = t;
    }

""", "", 1),
])
