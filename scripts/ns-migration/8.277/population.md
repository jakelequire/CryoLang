D31 population: 1368 distinct sites
  compiler/src        163   ident=137  member=19  scope=7
  examples              8   ident=1  member=3  scope=4
  stdlib              480   ident=268  member=40  scope=172
  tests/negative       26   ident=25  member=0  scope=1
  tests/projects       67   ident=39  member=12  scope=16
  tests/unit          571   ident=449  member=85  scope=37
  tools/CryoLSP        53   ident=36  member=17  scope=0
  by shape: ident=955  member=176  scope=237
  ident by follower: bare=8  call=142  scope-step=584  struct-lit=221
  files: 271
       45  stdlib/collections/array.cryo
       32  stdlib/thread/_module.cryo
       32  tests/tests/lang/async_generic_function.cryo
       30  stdlib/future/executor.cryo
       27  tests/tests/lang/generic_method_symbolic_receiver.cryo
       25  tests/tests/stdlib/async_stress_shapes.cryo
       22  compiler/src/compiler/codegen/ops/expr_ops.cryo
       21  tests/tests/lang/async_carry_across_await.cryo
       21  tests/tests/lang/static_match.cryo
       20  stdlib/alloc/arc.cryo
       20  tests/tests/stdlib/sync_atomic.cryo
       19  stdlib/collections/hashmap.cryo
       19  stdlib/io/buf.cryo
       18  stdlib/net/socket/udp.cryo
       18  stdlib/sync/mpsc.cryo
       18  tests/tests/lang/where_bound_nested_param_inference.cryo
       17  tests/tests/stdlib/io_async_traits.cryo
       16  compiler/src/compiler/decl_index.cryo
       16  stdlib/future/reactor.cryo
       16  stdlib/net/socket/tcp.cryo
       15  tests/tests/lang/async_receiver_refresh.cryo
       15  tests/tests/lang/method_generic_turbofish_literal.cryo
       15  tests/tests/stdlib/array.cryo
       14  stdlib/alloc/rc.cryo
       14  stdlib/net/http2/connection.cryo
       14  stdlib/net/tls/future.cryo
       14  tests/tests/lang/async_trait_method.cryo
       13  tests/tests/lang/send_sync_auto_derive.cryo
       12  compiler/src/compiler/compilation_context.cryo
       12  tests/tests/stdlib/math.cryo
       12  tests/tests/stdlib/net_tcp_conn.cryo
       11  stdlib/process/child.cryo
       11  stdlib/sync/rwlock.cryo
       11  tests/tests/lang/nested_generic_instantiation_arg.cryo
       11  tests/tests/projects/inherent_method_over_trait/src/main.cryo
       11  tests/tests/stdlib/net_http_conn.cryo
       10  compiler/src/compiler/types/generic_registry.cryo
        9  compiler/src/CLI/commands.cryo
        9  compiler/src/compiler/types/arena.cryo
        9  stdlib/collections/raw_buffer.cryo
        9  stdlib/collections/str.cryo
        9  stdlib/core/iter.cryo
        9  stdlib/sync/mutex.cryo
        9  tools/CryoLSP/src/handlers/code_action.cryo
        8  stdlib/alloc/box.cryo
        8  stdlib/core/cmp.cryo
        8  stdlib/future/blocking.cryo
        8  stdlib/io/traits.cryo
        8  stdlib/json/value.cryo
        8  tests/tests/stdlib/cmp.cryo
        8  tests/tests/stdlib/future_blocking.cryo
        8  tests/tests/stdlib/iter.cryo
        8  tests/tests/stdlib/json.cryo
        8  tests/tests/stdlib/net_http2.cryo
        7  stdlib/net/dns.cryo
        7  stdlib/net/http2/server.cryo
        7  stdlib/net/ws/conn.cryo
        7  tests/tests/lang/async_method.cryo
        7  tests/tests/lang/async_try_operator.cryo
        7  tests/tests/stdlib/net_http2_hpack.cryo
        7  tools/CryoLSP/src/server/session.cryo
        6  compiler/src/compiler/codegen/visit/call_emitter.cryo
        6  compiler/src/compiler/sema/sema.cryo
        6  examples/14-threads/src/main.cryo
        6  stdlib/collections/hashset.cryo
        6  stdlib/random/distribution.cryo
        6  stdlib/test/runner.cryo
        6  tests/tests/lang/async_pointer_across_await.cryo
        6  tests/tests/lang/generic_owner_numeric_context.cryo
        6  tests/tests/projects/bound_directed_static_path/src/main.cryo
        6  tests/tests/projects/default_param_keyed_by_symbol/src/main.cryo
        6  tests/tests/projects/module_qualified_static_call/tests/generic_owner_test.cryo
        6  tests/tests/stdlib/thread_local.cryo
        6  tests/tests/stdlib/varargs.cryo
        5  compiler/src/compiler/codegen/ops/declaration_emitter.cryo
        5  compiler/src/compiler/instance.cryo
        5  compiler/src/compiler/sema/state.cryo
        5  stdlib/future/combinator.cryo
        5  stdlib/net/http2/huffman.cryo
        5  tests/tests/lang/async_await_shapes.cryo
        5  tests/tests/lang/async_generic_owner_byvalue_arg.cryo
        5  tests/tests/lang/generics.cryo
        5  tests/tests/lang/match_generic_free_fn.cryo
        5  tests/tests/lang/qualified_module_call_arg_binding.cryo
        5  tests/tests/stdlib/net_tls_conn.cryo
        5  tests/tests/stdlib/net_udp.cryo
        5  tests/tests/stdlib/random.cryo
        5  tools/CryoLSP/src/handlers/code_lens.cryo
        5  tools/CryoLSP/src/protocol/conv.cryo
        4  compiler/src/CLI/_module.cryo
        4  compiler/src/compiler/deps/lockfile.cryo
        4  compiler/src/compiler/sema/lambda_synth.cryo
        4  compiler/src/compiler/vendor/registry.cryo
        4  stdlib/thread/local.cryo
        4  tests/tests/lang/async_if_expression.cryo
        4  tests/tests/lang/async_match_guard.cryo
        4  tests/tests/lang/async_void_output.cryo
        4  tests/tests/lang/f32_literal_args.cryo
        4  tests/tests/lang/generic_lookahead_grouping.cryo
        4  tests/tests/lang/generic_static_owner_binding.cryo
        4  tests/tests/lang/numeric_literal_suffix.cryo
        4  tests/tests/lang/operator_overload.cryo
        4  tests/tests/lang/trait_default_generics.cryo
        4  tests/tests/projects/generic_name_collision/tests/widget_static.cryo
        4  tests/tests/stdlib/future_combinator.cryo
        4  tests/tests/stdlib/http_buffered_parse.cryo
        4  tests/tests/stdlib/net_tcp.cryo
        4  tests/tests/stdlib/net_tcp_async.cryo
        4  tests/tests/stdlib/pair.cryo
        4  tests/tests/stdlib/sync_mpsc.cryo
        4  tests/tests/stdlib/thread_builder.cryo
        4  tools/CryoLSP/src/protocol/semantic_tokens.cryo
        3  compiler/src/compiler/codegen/passes.cryo
        3  stdlib/core/ptr.cryo
        3  stdlib/core/slice.cryo
        3  stdlib/net/http2/client.cryo
        3  stdlib/sync/barrier.cryo
        3  stdlib/sync/condvar.cryo
        3  stdlib/sync/once.cryo
        3  tests/tests/lang/array_length_type_identity.cryo
        3  tests/tests/lang/fluent_default_method_combinator.cryo
        3  tests/tests/lang/for_in.cryo
        3  tests/tests/lang/generic_method_on_temporary.cryo
        3  tests/tests/lang/impl_head_full_params.cryo
        3  tests/tests/lang/impl_trait_iter.cryo
        3  tests/tests/lang/operator_overloading.cryo
        3  tests/tests/lang/unions.cryo
        3  tests/tests/projects/generic_name_collision/tests/widget_static_test.cryo
        3  tests/tests/projects/impl_qualified_call/src/main.cryo
        3  tests/tests/projects/inherent_owner_leaf_collision/src/main.cryo
        3  tests/tests/stdlib/api_convergence.cryo
        3  tests/tests/stdlib/future_executor.cryo
        3  tests/tests/stdlib/iter_where_generic.cryo
        3  tests/tests/stdlib/net_http_server.cryo
        3  tests/tests/stdlib/net_tls_async.cryo
        3  tools/CryoLSP/src/handlers/completion.cryo
        3  tools/CryoLSP/src/handlers/lifecycle.cryo
        3  tools/CryoLSP/src/protocol/jsonrpc.cryo
        3  tools/CryoLSP/src/server/diag_render_cache.cryo
        2  compiler/src/compiler/codegen/type_map.cryo
        2  compiler/src/compiler/codegen/visit/decl_visit_emitter.cryo
        2  compiler/src/compiler/codegen/visit/new_delete_emitter.cryo
        2  compiler/src/compiler/const_table.cryo
        2  compiler/src/compiler/module_loader.cryo
        2  compiler/src/compiler/mono/state.cryo
        2  compiler/src/compiler/passes/dead_code.cryo
        2  compiler/src/compiler/passes/move_check.cryo
        2  compiler/src/compiler/passes/type_resolution.cryo
        2  compiler/src/compiler/resolver/resolution_map.cryo
        2  compiler/src/compiler/resolver/resolver.cryo
        2  compiler/src/compiler/resolver/scope.cryo
        2  examples/09-json-config/src/main.cryo
        2  stdlib/collections/pair.cryo
        2  stdlib/collections/string.cryo
        2  stdlib/core/hash.cryo
        2  stdlib/core/ops.cryo
        2  stdlib/core/panic_unwind.cryo
        2  stdlib/fmt/spec.cryo
        2  stdlib/fs/file.cryo
        2  stdlib/future/ready.cryo
        2  stdlib/io/cursor.cryo
        2  stdlib/net/http2/hpack.cryo
        2  stdlib/process/command.cryo
        2  tests/tests/lang/async_declaration_order.cryo
        2  tests/tests/lang/copy_bound.cryo
        2  tests/tests/lang/enum_discriminant_base.cryo
        2  tests/tests/lang/leaf_dispatch.cryo
        2  tests/tests/lang/local_type_inference.cryo
        2  tests/tests/lang/memory_layout.cryo
        2  tests/tests/lang/peel_ptr_bound_repro.cryo
        2  tests/tests/negative/E0010_generic_owner_arg_overflow.cryo
        2  tests/tests/negative/E0600_async_guard_moves_owning_payload.cryo
        2  tests/tests/negative/E0645_no_static_match_arm.cryo
        2  tests/tests/projects/generic_name_collision/tests/collision_test.cryo
        2  tests/tests/projects/generic_name_collision/tests/widget_mid.cryo
        2  tests/tests/projects/generic_name_collision/tests/widget_test.cryo
        2  tests/tests/projects/impl_concrete_arg_filters_impl/src/main.cryo
        2  tests/tests/projects/impl_concrete_arg_selects_impl/src/main.cryo
        2  tests/tests/projects/module_qualified_static_call/src/widget/deque.cryo
        2  tests/tests/projects/namespace_gate/src/orphan.cryo
        2  tests/tests/projects/namespace_gate/src/porter.cryo
        2  tests/tests/projects/plural_leaf_gate/src/orphan.cryo
        2  tests/tests/projects/resolution_leaf_index/tests/leaf_index_test.cryo
        2  tests/tests/stdlib/clone_owned.cryo
        2  tests/tests/stdlib/fmt.cryo
        2  tests/tests/stdlib/net_http_response_cap.cryo
        2  tests/tests/stdlib/net_ws.cryo
        2  tests/tests/stdlib/sync_arc.cryo
        2  tools/CryoLSP/src/handlers/inactive_regions.cryo
        2  tools/CryoLSP/src/handlers/semantic_tokens.cryo
        1  compiler/src/compiler/build_manifest.cryo
        1  compiler/src/compiler/codegen/ast_arena.cryo
        1  compiler/src/compiler/codegen/state/function_registry.cryo
        1  compiler/src/compiler/codegen/state/global_registry.cryo
        1  compiler/src/compiler/codegen/visit/ir_generator.cryo
        1  compiler/src/compiler/codegen/visit/pattern_emitter.cryo
        1  compiler/src/compiler/codegen/visit/place_emitter.cryo
        1  compiler/src/compiler/diag/renderer.cryo
        1  compiler/src/compiler/diag/sink.cryo
        1  compiler/src/compiler/module_graph.cryo
        1  compiler/src/compiler/mono/monomorphizer.cryo
        1  compiler/src/compiler/passes/specialization.cryo
        1  compiler/src/compiler/resolver/intern_table.cryo
        1  compiler/src/compiler/sema/call_resolver.cryo
        1  compiler/src/compiler/types/resolver.cryo
        1  stdlib/encoding/base64.cryo
        1  stdlib/encoding/sha1.cryo
        1  stdlib/env/_module.cryo
        1  stdlib/fmt/display.cryo
        1  stdlib/fmt/interp.cryo
        1  stdlib/json/parser.cryo
        1  stdlib/math/_module.cryo
        1  stdlib/net/http/client.cryo
        1  stdlib/net/http/response.cryo
        1  stdlib/net/http/server.cryo
        1  stdlib/net/https.cryo
        1  stdlib/net/ws/handshake.cryo
        1  stdlib/sync/atomic.cryo
        1  stdlib/time/datetime.cryo
        1  tests/tests/lang/deref_take_in_unsafe.cryo
        1  tests/tests/lang/drop_glue.cryo
        1  tests/tests/lang/fn_pointer_shadowing.cryo
        1  tests/tests/lang/int128.cryo
        1  tests/tests/lang/lambdas.cryo
        1  tests/tests/lang/match_subject_owned_place.cryo
        1  tests/tests/lang/nested_match_conditional_move.cryo
        1  tests/tests/lang/sized_int.cryo
        1  tests/tests/lang/type_where_bound.cryo
        1  tests/tests/negative/E0203_type_argument_undeclared.cryo
        1  tests/tests/negative/E0302_trait_head_undeclared_param.cryo
        1  tests/tests/negative/E0306_separate_block_drop_not_copy.cryo
        1  tests/tests/negative/E0306_trait_bound.cryo
        1  tests/tests/negative/E0306_type_where_bound.cryo
        1  tests/tests/negative/E0307_owner_literal_default.cryo
        1  tests/tests/negative/E0311_generic_param_shadowed.cryo
        1  tests/tests/negative/E0353_private_field_async_foreign.cryo
        1  tests/tests/negative/E0453_deref_move_out_qualified_call.cryo
        1  tests/tests/negative/E0455_async_address_in_if_expression.cryo
        1  tests/tests/negative/E0455_async_address_into_awaited_future.cryo
        1  tests/tests/negative/E0455_async_pointer_outlives_local.cryo
        1  tests/tests/negative/E0455_async_stored_method_future.cryo
        1  tests/tests/negative/E0455_async_temporary_receiver.cryo
        1  tests/tests/negative/E0458_closure_into_generic_free_fn.cryo
        1  tests/tests/negative/E0459_future_moved_after_poll.cryo
        1  tests/tests/negative/E0600_async_mutually_recursive_futures.cryo
        1  tests/tests/negative/E0600_async_recursive_future.cryo
        1  tests/tests/negative/E0600_await_in_static_match_arm.cryo
        1  tests/tests/negative/E0600_await_in_unsafe_block.cryo
        1  tests/tests/projects/inherent_owner_leaf_collision/src/generic.cryo
        1  tests/tests/projects/inline_generic_bound/src/main.cryo
        1  tests/tests/projects/resolution_leaf_index/tests/importer.cryo
        1  tests/tests/projects/resolution_tripwire/tests/tripwire_test.cryo
        1  tests/tests/projects/type_where_bound_inferred/src/main.cryo
        1  tests/tests/stdlib/alloc_realloc_align.cryo
        1  tests/tests/stdlib/async_branch_owning_local.cryo
        1  tests/tests/stdlib/fmt_print_family.cryo
        1  tests/tests/stdlib/fstring.cryo
        1  tests/tests/stdlib/io_cursor.cryo
        1  tests/tests/stdlib/io_stdio_lock.cryo
        1  tests/tests/stdlib/net_dns.cryo
        1  tests/tests/stdlib/net_https.cryo
        1  tests/tests/stdlib/process_command.cryo
        1  tests/tests/stdlib/rc.cryo
        1  tests/tests/stdlib/test_fixture_static_dispatch.cryo
        1  tools/CryoLSP/src/handlers/definition.cryo
        1  tools/CryoLSP/src/handlers/diagnostics.cryo
        1  tools/CryoLSP/src/handlers/hover.cryo
        1  tools/CryoLSP/src/handlers/rendered_diagnostic.cryo
        1  tools/CryoLSP/src/handlers/text_sync.cryo
        1  tools/CryoLSP/src/server/docs.cryo
        1  tools/CryoLSP/src/server/line_index.cryo
  ident sites the scan alone decided (not declared generic in the writing module): 565
    compiler/src     compiler/src/compiler/build_manifest.cryo:298:38  Array
    compiler/src     compiler/src/compiler/codegen/ast_arena.cryo:48:34  Box
    compiler/src     compiler/src/compiler/codegen/ops/declaration_emitter.cryo:1826:19  Pair
    compiler/src     compiler/src/compiler/codegen/ops/declaration_emitter.cryo:1827:19  Pair
    compiler/src     compiler/src/compiler/codegen/ops/declaration_emitter.cryo:1853:19  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:1564:27  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:1565:27  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2255:26  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2323:29  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2324:29  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2333:27  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2334:27  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2348:30  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2349:30  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2367:26  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2390:30  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2391:30  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2393:30  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2418:24  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2421:28  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2513:20  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2530:19  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:2748:30  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:655:29  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:661:33  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:713:29  Pair
    compiler/src     compiler/src/compiler/codegen/ops/expr_ops.cryo:719:33  Pair
    compiler/src     compiler/src/compiler/codegen/passes.cryo:1225:28  Slice
    compiler/src     compiler/src/compiler/codegen/passes.cryo:759:46  Box
    compiler/src     compiler/src/compiler/codegen/passes.cryo:804:51  Box
    compiler/src     compiler/src/compiler/codegen/state/function_registry.cryo:32:31  HashMap
    compiler/src     compiler/src/compiler/codegen/state/global_registry.cryo:25:53  HashMap
    compiler/src     compiler/src/compiler/codegen/type_map.cryo:75:41  HashMap
    compiler/src     compiler/src/compiler/codegen/type_map.cryo:93:48  Box
    compiler/src     compiler/src/compiler/codegen/visit/call_emitter.cryo:328:48  Pair
    compiler/src     compiler/src/compiler/codegen/visit/call_emitter.cryo:331:52  Pair
    compiler/src     compiler/src/compiler/codegen/visit/call_emitter.cryo:403:44  Pair
    compiler/src     compiler/src/compiler/codegen/visit/call_emitter.cryo:584:35  Pair
    compiler/src     compiler/src/compiler/codegen/visit/call_emitter.cryo:832:23  Pair
    compiler/src     compiler/src/compiler/codegen/visit/call_emitter.cryo:865:23  Pair
    compiler/src     compiler/src/compiler/codegen/visit/decl_visit_emitter.cryo:334:24  Pair
    compiler/src     compiler/src/compiler/codegen/visit/decl_visit_emitter.cryo:376:28  Pair
    compiler/src     compiler/src/compiler/codegen/visit/new_delete_emitter.cryo:327:32  Pair
    compiler/src     compiler/src/compiler/codegen/visit/new_delete_emitter.cryo:330:36  Pair
    compiler/src     compiler/src/compiler/compilation_context.cryo:255:50  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:258:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:259:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:260:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:261:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:262:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:263:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:264:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:676:48  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:684:53  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:692:48  Box
    compiler/src     compiler/src/compiler/compilation_context.cryo:711:45  Box
    compiler/src     compiler/src/compiler/const_table.cryo:128:33  HashMap
    compiler/src     compiler/src/compiler/const_table.cryo:130:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:1086:29  Pair
    compiler/src     compiler/src/compiler/decl_index.cryo:323:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:324:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:325:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:326:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:327:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:328:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:343:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:344:34  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:345:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:346:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:347:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:349:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:350:33  HashMap
    compiler/src     compiler/src/compiler/decl_index.cryo:383:13  Pair
    compiler/src     compiler/src/compiler/decl_index.cryo:548:49  Pair
    compiler/src     compiler/src/compiler/deps/lockfile.cryo:211:38  Array
    compiler/src     compiler/src/compiler/deps/lockfile.cryo:223:38  Array
    compiler/src     compiler/src/compiler/diag/renderer.cryo:179:30  HashMap
    compiler/src     compiler/src/compiler/diag/sink.cryo:60:32  HashMap
    compiler/src     compiler/src/compiler/instance.cryo:1083:49  Box
    compiler/src     compiler/src/compiler/instance.cryo:606:51  Box
    compiler/src     compiler/src/compiler/instance.cryo:949:51  Box
    compiler/src     compiler/src/compiler/module_graph.cryo:247:31  HashMap
    compiler/src     compiler/src/compiler/module_loader.cryo:135:28  HashMap
    compiler/src     compiler/src/compiler/module_loader.cryo:136:28  HashMap
    compiler/src     compiler/src/compiler/mono/monomorphizer.cryo:141:36  HashMap
    compiler/src     compiler/src/compiler/mono/state.cryo:173:31  HashMap
    compiler/src     compiler/src/compiler/mono/state.cryo:174:33  HashMap
    compiler/src     compiler/src/compiler/passes/dead_code.cryo:124:28  HashMap
    compiler/src     compiler/src/compiler/passes/dead_code.cryo:125:28  HashMap
    compiler/src     compiler/src/compiler/passes/move_check.cryo:235:26  HashMap
    compiler/src     compiler/src/compiler/passes/move_check.cryo:257:28  HashMap
    compiler/src     compiler/src/compiler/passes/specialization.cryo:278:51  Box
    compiler/src     compiler/src/compiler/resolver/intern_table.cryo:40:22  HashMap
    compiler/src     compiler/src/compiler/resolver/resolution_map.cryo:28:24  HashMap
    compiler/src     compiler/src/compiler/resolver/resolution_map.cryo:29:24  HashMap
    compiler/src     compiler/src/compiler/resolver/resolver.cryo:115:35  HashMap
    compiler/src     compiler/src/compiler/resolver/resolver.cryo:450:27  Pair
    compiler/src     compiler/src/compiler/resolver/scope.cryo:180:33  HashMap
    compiler/src     compiler/src/compiler/resolver/scope.cryo:340:29  Pair
    compiler/src     compiler/src/compiler/sema/call_resolver.cryo:2833:46  Slice
    compiler/src     compiler/src/compiler/sema/lambda_synth.cryo:164:55  HashMap
    compiler/src     compiler/src/compiler/sema/lambda_synth.cryo:180:37  HashMap
    compiler/src     compiler/src/compiler/sema/lambda_synth.cryo:181:37  HashMap
    compiler/src     compiler/src/compiler/sema/lambda_synth.cryo:182:37  HashMap
    compiler/src     compiler/src/compiler/sema/sema.cryo:290:42  Box
    compiler/src     compiler/src/compiler/sema/sema.cryo:3915:42  Box
    compiler/src     compiler/src/compiler/sema/sema.cryo:897:37  HashMap
    compiler/src     compiler/src/compiler/sema/sema.cryo:898:37  HashMap
    compiler/src     compiler/src/compiler/sema/sema.cryo:899:37  HashMap
    compiler/src     compiler/src/compiler/sema/sema.cryo:916:42  HashMap
    compiler/src     compiler/src/compiler/sema/state.cryo:171:29  HashMap
    compiler/src     compiler/src/compiler/sema/state.cryo:172:29  HashMap
    compiler/src     compiler/src/compiler/sema/state.cryo:173:29  HashMap
    compiler/src     compiler/src/compiler/sema/state.cryo:192:34  HashMap
    compiler/src     compiler/src/compiler/sema/state.cryo:195:31  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:194:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:195:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:196:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:197:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:198:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:199:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:200:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:201:34  HashMap
    compiler/src     compiler/src/compiler/types/arena.cryo:202:34  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:1452:26  Pair
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:373:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:374:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:377:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:378:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:381:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:383:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:384:32  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:387:35  HashMap
    compiler/src     compiler/src/compiler/types/generic_registry.cryo:392:30  HashMap
    compiler/src     compiler/src/compiler/types/resolver.cryo:150:34  Pair
    compiler/src     compiler/src/compiler/vendor/registry.cryo:297:38  Array
    compiler/src     compiler/src/compiler/vendor/registry.cryo:526:39  Array
    examples         examples/14-threads/src/main.cryo:100:31  Atomic
    stdlib           stdlib/alloc/arc.cryo:133:28  Atomic
    stdlib           stdlib/alloc/arc.cryo:134:28  Atomic
    stdlib           stdlib/alloc/arc.cryo:149:28  Atomic
    stdlib           stdlib/alloc/arc.cryo:150:28  Atomic
    stdlib           stdlib/collections/array.cryo:170:16  SliceIter
    stdlib           stdlib/collections/hashmap.cryo:608:37  Pair
    stdlib           stdlib/collections/hashmap.cryo:705:37  Pair
    stdlib           stdlib/collections/str.cryo:380:16  Pair
    stdlib           stdlib/collections/str.cryo:397:16  Pair
    stdlib           stdlib/collections/str.cryo:402:16  Pair
    stdlib           stdlib/collections/str.cryo:409:20  Pair
    stdlib           stdlib/collections/str.cryo:418:16  Pair
    stdlib           stdlib/collections/str.cryo:421:16  Pair
    stdlib           stdlib/collections/str.cryo:424:16  Pair
    stdlib           stdlib/collections/str.cryo:427:12  Pair
    stdlib           stdlib/collections/str.cryo:467:29  Pair
    stdlib           stdlib/core/iter.cryo:360:37  Pair
    stdlib           stdlib/core/iter.cryo:387:45  Pair
    stdlib           stdlib/encoding/base64.cryo:62:26  Array
    stdlib           stdlib/encoding/sha1.cryo:33:26  Array
    stdlib           stdlib/env/_module.cryo:117:18  Pair
    stdlib           stdlib/fmt/interp.cryo:117:20  fmt_display_body
    stdlib           stdlib/fmt/spec.cryo:246:16  format_to_string
    stdlib           stdlib/fmt/spec.cryo:261:12  format_to_string
    stdlib           stdlib/fs/file.cryo:269:20  Slice
    stdlib           stdlib/fs/file.cryo:315:19  Slice
    stdlib           stdlib/future/blocking.cryo:126:23  Atomic
    stdlib           stdlib/future/blocking.cryo:127:23  Atomic
    stdlib           stdlib/future/executor.cryo:251:25  Atomic
    stdlib           stdlib/future/executor.cryo:434:5  Arc
    stdlib           stdlib/future/executor.cryo:442:5  Arc
    stdlib           stdlib/future/executor.cryo:700:22  Atomic
    stdlib           stdlib/future/executor.cryo:701:22  Atomic
    stdlib           stdlib/future/executor.cryo:702:22  Atomic
    stdlib           stdlib/future/reactor.cryo:176:23  Atomic
    stdlib           stdlib/io/buf.cryo:502:22  Array
    stdlib           stdlib/io/buf.cryo:504:22  Array
    stdlib           stdlib/io/buf.cryo:505:22  Array
    stdlib           stdlib/io/buf.cryo:546:29  Array
    stdlib           stdlib/io/buf.cryo:579:16  Slice
    stdlib           stdlib/io/buf.cryo:595:34  Array
    stdlib           stdlib/io/buf.cryo:617:35  Array
    stdlib           stdlib/io/cursor.cryo:47:33  Array
    stdlib           stdlib/io/cursor.cryo:79:30  Array
    stdlib           stdlib/io/traits.cryo:220:43  Slice
    stdlib           stdlib/io/traits.cryo:328:28  Slice
    stdlib           stdlib/io/traits.cryo:487:30  Array
    stdlib           stdlib/io/traits.cryo:529:35  Array
    stdlib           stdlib/io/traits.cryo:642:28  Slice
    stdlib           stdlib/io/traits.cryo:681:30  Array
    stdlib           stdlib/json/parser.cryo:538:37  Array
    stdlib           stdlib/json/value.cryo:111:21  Array
    stdlib           stdlib/json/value.cryo:112:21  Array
    stdlib           stdlib/json/value.cryo:113:21  HashMap
    stdlib           stdlib/json/value.cryo:119:21  Array
    stdlib           stdlib/json/value.cryo:120:21  Array
    stdlib           stdlib/json/value.cryo:121:21  HashMap
    stdlib           stdlib/json/value.cryo:257:37  Array
    stdlib           stdlib/json/value.cryo:265:33  Array
    stdlib           stdlib/net/dns.cryo:141:32  Array
    stdlib           stdlib/net/dns.cryo:177:34  Array
    stdlib           stdlib/net/dns.cryo:343:67  Array
    stdlib           stdlib/net/http/client.cryo:101:35  BufStream
    stdlib           stdlib/net/http/response.cryo:168:32  Slice
    stdlib           stdlib/net/http/server.cryo:203:49  BufStream
    stdlib           stdlib/net/http2/client.cryo:50:33  Http2Connection
    stdlib           stdlib/net/http2/client.cryo:88:48  BufStream
    stdlib           stdlib/net/http2/client.cryo:88:9  Http2Connection
    stdlib           stdlib/net/http2/connection.cryo:237:34  Array
    stdlib           stdlib/net/http2/connection.cryo:446:32  Array
    stdlib           stdlib/net/http2/connection.cryo:453:37  Array
    stdlib           stdlib/net/http2/connection.cryo:473:32  Array
    stdlib           stdlib/net/http2/connection.cryo:474:31  Array
    stdlib           stdlib/net/http2/connection.cryo:563:32  Array
    stdlib           stdlib/net/http2/connection.cryo:564:31  Array
    stdlib           stdlib/net/http2/connection.cryo:644:32  Array
    stdlib           stdlib/net/http2/connection.cryo:650:37  Array
    stdlib           stdlib/net/http2/hpack.cryo:319:39  Array
    stdlib           stdlib/net/http2/hpack.cryo:89:23  Array
    stdlib           stdlib/net/http2/huffman.cryo:133:26  Array
    stdlib           stdlib/net/http2/huffman.cryo:168:19  Array
    stdlib           stdlib/net/http2/huffman.cryo:169:19  Array
    stdlib           stdlib/net/http2/huffman.cryo:170:19  Array
    stdlib           stdlib/net/http2/huffman.cryo:228:26  Array
    stdlib           stdlib/net/http2/server.cryo:49:33  Http2Connection
    stdlib           stdlib/net/http2/server.cryo:76:13  Http2Connection
    stdlib           stdlib/net/http2/server.cryo:76:52  BufStream
    stdlib           stdlib/net/http2/server.cryo:87:17  Http2Connection
    stdlib           stdlib/net/http2/server.cryo:87:56  BufStream
    stdlib           stdlib/net/https.cryo:127:39  BufStream
    stdlib           stdlib/net/socket/tcp.cryo:255:30  Array
    stdlib           stdlib/net/socket/tcp.cryo:330:28  Array
    stdlib           stdlib/net/socket/tcp.cryo:397:28  Array
    stdlib           stdlib/net/socket/udp.cryo:204:30  Array
    stdlib           stdlib/net/socket/udp.cryo:231:30  Array
    stdlib           stdlib/net/socket/udp.cryo:270:28  Array
    stdlib           stdlib/net/socket/udp.cryo:343:28  Array
    stdlib           stdlib/net/socket/udp.cryo:411:28  Array
    stdlib           stdlib/net/socket/udp.cryo:481:28  Array
    stdlib           stdlib/net/tls/future.cryo:204:28  Array
    stdlib           stdlib/net/tls/future.cryo:268:28  Array
    stdlib           stdlib/net/tls/future.cryo:84:30  Array
    stdlib           stdlib/net/ws/conn.cryo:207:34  Array
    stdlib           stdlib/net/ws/conn.cryo:224:30  Array
    stdlib           stdlib/net/ws/conn.cryo:313:34  BufStream
    stdlib           stdlib/net/ws/conn.cryo:322:34  BufStream
    stdlib           stdlib/net/ws/handshake.cryo:38:26  Array
    stdlib           stdlib/process/child.cryo:1022:36  Array
    stdlib           stdlib/process/child.cryo:916:28  Array
    stdlib           stdlib/process/child.cryo:986:26  Array
    stdlib           stdlib/test/runner.cryo:1844:36  Atomic
    stdlib           stdlib/test/runner.cryo:885:35  Slice
    stdlib           stdlib/thread/_module.cryo:389:23  Atomic
    stdlib           stdlib/time/datetime.cryo:96:16  format_to_string
    tests/negative   tests/tests/negative/E0203_type_argument_undeclared.cryo:13:27  String
    tests/negative   tests/tests/negative/E0353_private_field_async_foreign.cryo:21:33  Ready
    tests/negative   tests/tests/negative/E0455_async_address_in_if_expression.cryo:19:27  PendingThenReady
    tests/negative   tests/tests/negative/E0455_async_address_into_awaited_future.cryo:17:26  PendingThenReady
    tests/negative   tests/tests/negative/E0455_async_pointer_outlives_local.cryo:18:27  PendingThenReady
    tests/negative   tests/tests/negative/E0455_async_stored_method_future.cryo:24:30  PendingThenReady
    tests/negative   tests/tests/negative/E0455_async_temporary_receiver.cryo:24:30  PendingThenReady
    tests/negative   tests/tests/negative/E0459_future_moved_after_poll.cryo:21:26  Ready
    tests/negative   tests/tests/negative/E0600_async_guard_moves_owning_payload.cryo:24:18  PendingThenReady
    tests/negative   tests/tests/negative/E0600_async_guard_moves_owning_payload.cryo:30:35  PendingThenReady
    tests/negative   tests/tests/negative/E0600_async_mutually_recursive_futures.cryo:21:27  PendingThenReady
    tests/negative   tests/tests/negative/E0600_async_recursive_future.cryo:17:27  PendingThenReady
    tests/negative   tests/tests/negative/E0600_await_in_static_match_arm.cryo:21:18  PendingThenReady
    tests/negative   tests/tests/negative/E0600_await_in_unsafe_block.cryo:21:18  PendingThenReady
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_mid.cryo:19:16  Widget
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_mid.cryo:25:16  Widget
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_static.cryo:29:16  Widget
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_static.cryo:34:16  Widget
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_static.cryo:44:12  Widget
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_static.cryo:58:24  Widget
    tests/projects   tests/tests/projects/generic_name_collision/tests/widget_static_test.cryo:59:60  wrap
    tests/projects   tests/tests/projects/inherent_owner_leaf_collision/src/main.cryo:8:26  Pair
    tests/projects   tests/tests/projects/namespace_gate/src/orphan.cryo:37:16  Crate
    tests/projects   tests/tests/projects/namespace_gate/src/orphan.cryo:43:12  Crate
    tests/projects   tests/tests/projects/namespace_gate/src/porter.cryo:15:12  Crate
    tests/projects   tests/tests/projects/plural_leaf_gate/src/orphan.cryo:17:16  Widget
    tests/projects   tests/tests/projects/plural_leaf_gate/src/orphan.cryo:23:12  Widget
    tests/unit       tests/tests/lang/async_await_shapes.cryo:62:18  PendingThenReady
    tests/unit       tests/tests/lang/async_await_shapes.cryo:66:18  PendingThenReady
    tests/unit       tests/tests/lang/async_await_shapes.cryo:70:18  PendingThenReady
    tests/unit       tests/tests/lang/async_await_shapes.cryo:74:18  PendingThenReady
    tests/unit       tests/tests/lang/async_await_shapes.cryo:78:18  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:101:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:214:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:224:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:238:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:250:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:255:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:262:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:39:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:41:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:47:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:49:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:55:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:58:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:64:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:69:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:77:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:79:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:88:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:89:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:91:21  PendingThenReady
    tests/unit       tests/tests/lang/async_carry_across_await.cryo:99:21  PendingThenReady
    tests/unit       tests/tests/lang/async_declaration_order.cryo:28:27  PendingThenReady
    tests/unit       tests/tests/lang/async_declaration_order.cryo:40:27  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:102:29  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:110:30  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:113:30  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:118:30  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:63:29  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:70:24  Ready
    tests/unit       tests/tests/lang/async_generic_function.cryo:85:29  PendingThenReady
    tests/unit       tests/tests/lang/async_generic_function.cryo:96:29  PendingThenReady
    tests/unit       tests/tests/lang/async_if_expression.cryo:48:30  PendingThenReady
    tests/unit       tests/tests/lang/async_if_expression.cryo:57:35  PendingThenReady
    tests/unit       tests/tests/lang/async_if_expression.cryo:65:26  PendingThenReady
    tests/unit       tests/tests/lang/async_if_expression.cryo:79:26  PendingThenReady
    tests/unit       tests/tests/lang/async_match_guard.cryo:211:18  PendingThenReady
    tests/unit       tests/tests/lang/async_match_guard.cryo:39:18  PendingThenReady
    tests/unit       tests/tests/lang/async_match_guard.cryo:43:18  PendingThenReady
    tests/unit       tests/tests/lang/async_match_guard.cryo:47:18  PendingThenReady
    tests/unit       tests/tests/lang/async_pointer_across_await.cryo:121:30  PendingThenReady
    tests/unit       tests/tests/lang/async_pointer_across_await.cryo:136:27  PendingThenReady
    tests/unit       tests/tests/lang/async_pointer_across_await.cryo:33:27  PendingThenReady
    tests/unit       tests/tests/lang/async_pointer_across_await.cryo:43:27  PendingThenReady
    tests/unit       tests/tests/lang/async_pointer_across_await.cryo:51:27  PendingThenReady
    tests/unit       tests/tests/lang/async_pointer_across_await.cryo:63:27  PendingThenReady
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:104:37  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:120:56  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:130:44  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:158:30  PendingThenReady
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:187:40  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:194:40  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:227:40  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:290:40  Array
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:43:30  PendingThenReady
    tests/unit       tests/tests/lang/async_receiver_refresh.cryo:65:54  Array
    tests/unit       tests/tests/lang/async_trait_method.cryo:56:33  PendingThenReady
    tests/unit       tests/tests/lang/async_trait_method.cryo:67:33  PendingThenReady
    tests/unit       tests/tests/lang/async_try_operator.cryo:117:33  PendingThenReady
    tests/unit       tests/tests/lang/async_try_operator.cryo:276:26  PendingThenReady
    tests/unit       tests/tests/lang/async_try_operator.cryo:41:26  PendingThenReady
    tests/unit       tests/tests/lang/async_try_operator.cryo:56:26  PendingThenReady
    tests/unit       tests/tests/lang/async_try_operator.cryo:82:26  PendingThenReady
    tests/unit       tests/tests/lang/async_void_output.cryo:35:27  PendingThenReady
    tests/unit       tests/tests/lang/async_void_output.cryo:40:27  PendingThenReady
    tests/unit       tests/tests/lang/async_void_output.cryo:49:27  PendingThenReady
    tests/unit       tests/tests/lang/async_void_output.cryo:54:27  PendingThenReady
    tests/unit       tests/tests/lang/copy_bound.cryo:75:30  Array
    tests/unit       tests/tests/lang/copy_bound.cryo:93:29  Array
    tests/unit       tests/tests/lang/drop_glue.cryo:193:32  Array
    tests/unit       tests/tests/lang/for_in.cryo:289:27  Array
    tests/unit       tests/tests/lang/for_in.cryo:307:29  Array
    tests/unit       tests/tests/lang/for_in.cryo:317:27  Array
    tests/unit       tests/tests/lang/local_type_inference.cryo:66:14  Range
    tests/unit       tests/tests/lang/local_type_inference.cryo:76:25  Array
    tests/unit       tests/tests/lang/method_generic_turbofish_literal.cryo:150:37  Array
    tests/unit       tests/tests/lang/method_generic_turbofish_literal.cryo:80:28  Slice
    tests/unit       tests/tests/lang/nested_match_conditional_move.cryo:49:46  Array
    tests/unit       tests/tests/lang/send_sync_auto_derive.cryo:103:26  Atomic
    tests/unit       tests/tests/lang/send_sync_auto_derive.cryo:139:40  Atomic
    tests/unit       tests/tests/stdlib/alloc_realloc_align.cryo:33:33  Array
    tests/unit       tests/tests/stdlib/api_convergence.cryo:324:27  HashSet
    tests/unit       tests/tests/stdlib/api_convergence.cryo:485:32  Array
    tests/unit       tests/tests/stdlib/api_convergence.cryo:487:32  HashMap
    tests/unit       tests/tests/stdlib/array.cryo:327:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:336:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:350:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:354:28  Array
    tests/unit       tests/tests/stdlib/array.cryo:365:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:369:28  Array
    tests/unit       tests/tests/stdlib/array.cryo:381:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:386:28  Array
    tests/unit       tests/tests/stdlib/array.cryo:400:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:428:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:433:28  Array
    tests/unit       tests/tests/stdlib/array.cryo:452:25  Array
    tests/unit       tests/tests/stdlib/array.cryo:455:28  Array
    tests/unit       tests/tests/stdlib/array.cryo:472:24  Array
    tests/unit       tests/tests/stdlib/array.cryo:507:24  Array
    tests/unit       tests/tests/stdlib/async_branch_owning_local.cryo:60:28  Array
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1031:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1038:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1048:23  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1051:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1059:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1109:28  Array
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1128:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1143:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1244:26  Array
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:1289:27  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:130:30  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:156:30  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:167:18  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:186:26  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:62:35  Array
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:75:30  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:82:30  PendingThenReady
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:833:26  Ready
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:838:26  Ready
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:844:26  Ready
    tests/unit       tests/tests/stdlib/async_stress_shapes.cryo:918:26  PendingThenReady
    tests/unit       tests/tests/stdlib/clone_owned.cryo:104:38  HashMap
    tests/unit       tests/tests/stdlib/clone_owned.cryo:62:28  Array
    tests/unit       tests/tests/stdlib/fmt.cryo:642:25  Array
    tests/unit       tests/tests/stdlib/fmt.cryo:656:33  Array
    tests/unit       tests/tests/stdlib/fmt_print_family.cryo:211:26  Array
    tests/unit       tests/tests/stdlib/fstring.cryo:125:25  Array
    tests/unit       tests/tests/stdlib/future_blocking.cryo:131:17  Atomic
    tests/unit       tests/tests/stdlib/future_blocking.cryo:147:10  Atomic
    tests/unit       tests/tests/stdlib/future_combinator.cryo:370:39  Array
    tests/unit       tests/tests/stdlib/future_combinator.cryo:377:39  Array
    tests/unit       tests/tests/stdlib/future_executor.cryo:73:26  Ready
    tests/unit       tests/tests/stdlib/future_executor.cryo:77:26  Ready
    tests/unit       tests/tests/stdlib/http_buffered_parse.cryo:142:38  BufStream
    tests/unit       tests/tests/stdlib/http_buffered_parse.cryo:159:9  BufStream
    tests/unit       tests/tests/stdlib/http_buffered_parse.cryo:61:28  Array
    tests/unit       tests/tests/stdlib/http_buffered_parse.cryo:71:58  Array
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:113:16  Slice
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:130:32  Array
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:130:56  Array
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:143:34  PendingThenReady
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:54:28  Array
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:64:51  Array
    tests/unit       tests/tests/stdlib/io_async_traits.cryo:96:34  PendingThenReady
    tests/unit       tests/tests/stdlib/io_cursor.cryo:80:33  Array
    tests/unit       tests/tests/stdlib/io_stdio_lock.cryo:32:22  Slice
    tests/unit       tests/tests/stdlib/iter.cryo:1001:38  Range
    tests/unit       tests/tests/stdlib/iter.cryo:396:22  Range
    tests/unit       tests/tests/stdlib/iter.cryo:404:22  Range
    tests/unit       tests/tests/stdlib/iter.cryo:428:22  Range
    tests/unit       tests/tests/stdlib/iter.cryo:457:15  Range
    tests/unit       tests/tests/stdlib/iter.cryo:459:15  Range
    tests/unit       tests/tests/stdlib/iter.cryo:888:39  Range
    tests/unit       tests/tests/stdlib/iter.cryo:985:38  Range
    tests/unit       tests/tests/stdlib/iter_where_generic.cryo:45:37  Pair
    tests/unit       tests/tests/stdlib/iter_where_generic.cryo:55:25  Range
    tests/unit       tests/tests/stdlib/net_dns.cryo:89:53  Array
    tests/unit       tests/tests/stdlib/net_http2.cryo:111:27  Array
    tests/unit       tests/tests/stdlib/net_http2.cryo:189:27  Array
    tests/unit       tests/tests/stdlib/net_http2.cryo:227:48  BufStream
    tests/unit       tests/tests/stdlib/net_http2.cryo:227:9  Http2Connection
    tests/unit       tests/tests/stdlib/net_http2.cryo:269:48  BufStream
    tests/unit       tests/tests/stdlib/net_http2.cryo:269:9  Http2Connection
    tests/unit       tests/tests/stdlib/net_http2.cryo:301:48  BufStream
    tests/unit       tests/tests/stdlib/net_http2.cryo:301:9  Http2Connection
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:139:24  Array
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:147:24  Array
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:204:25  Array
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:224:25  Array
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:251:24  Array
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:266:25  Array
    tests/unit       tests/tests/stdlib/net_http2_hpack.cryo:33:26  Array
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:169:9  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:210:38  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:231:38  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:254:9  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:258:30  Array
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:267:30  Array
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:273:9  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:317:35  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:348:35  BufStream
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:84:28  Array
    tests/unit       tests/tests/stdlib/net_http_conn.cryo:94:58  Array
    tests/unit       tests/tests/stdlib/net_http_response_cap.cryo:51:28  Array
    tests/unit       tests/tests/stdlib/net_http_response_cap.cryo:96:38  BufStream
    tests/unit       tests/tests/stdlib/net_http_server.cryo:222:35  BufStream
    tests/unit       tests/tests/stdlib/net_http_server.cryo:279:35  BufStream
    tests/unit       tests/tests/stdlib/net_http_server.cryo:95:35  BufStream
    tests/unit       tests/tests/stdlib/net_https.cryo:112:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp.cryo:75:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp.cryo:94:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp_async.cryo:107:27  Array
    tests/unit       tests/tests/stdlib/net_tcp_async.cryo:159:27  Array
    tests/unit       tests/tests/stdlib/net_tcp_async.cryo:62:27  Array
    tests/unit       tests/tests/stdlib/net_tcp_async.cryo:92:26  Array
    tests/unit       tests/tests/stdlib/net_tcp_conn.cryo:114:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp_conn.cryo:164:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp_conn.cryo:179:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp_conn.cryo:220:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp_conn.cryo:246:35  BufStream
    tests/unit       tests/tests/stdlib/net_tcp_conn.cryo:84:35  BufStream
    tests/unit       tests/tests/stdlib/net_tls_async.cryo:117:26  Array
    tests/unit       tests/tests/stdlib/net_tls_async.cryo:128:27  Array
    tests/unit       tests/tests/stdlib/net_tls_async.cryo:75:27  Array
    tests/unit       tests/tests/stdlib/net_tls_conn.cryo:101:35  BufStream
    tests/unit       tests/tests/stdlib/net_tls_conn.cryo:142:35  BufStream
    tests/unit       tests/tests/stdlib/net_udp.cryo:167:28  Array
    tests/unit       tests/tests/stdlib/net_udp.cryo:228:26  Array
    tests/unit       tests/tests/stdlib/net_udp.cryo:48:24  Array
    tests/unit       tests/tests/stdlib/net_udp.cryo:55:24  Array
    tests/unit       tests/tests/stdlib/net_udp.cryo:64:24  Array
    tests/unit       tests/tests/stdlib/net_ws.cryo:263:35  BufStream
    tests/unit       tests/tests/stdlib/net_ws.cryo:311:36  WebSocket
    tests/unit       tests/tests/stdlib/pair.cryo:210:18  Pair
    tests/unit       tests/tests/stdlib/pair.cryo:211:18  Pair
    tests/unit       tests/tests/stdlib/pair.cryo:212:18  Pair
    tests/unit       tests/tests/stdlib/pair.cryo:74:31  Pair
    tests/unit       tests/tests/stdlib/rc.cryo:232:28  Weak
    tests/unit       tests/tests/stdlib/sync_arc.cryo:149:39  Array
    tests/unit       tests/tests/stdlib/sync_arc.cryo:284:28  Weak
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:124:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:141:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:166:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:194:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:207:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:221:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:240:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:256:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:271:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:27:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:299:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:317:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:330:26  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:345:30  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:356:30  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:368:30  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:39:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:52:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:69:25  Atomic
    tests/unit       tests/tests/stdlib/sync_atomic.cryo:86:25  Atomic
    tests/unit       tests/tests/stdlib/thread_local.cryo:108:32  ThreadLocal
    tests/unit       tests/tests/stdlib/thread_local.cryo:130:32  ThreadLocal
    tests/unit       tests/tests/stdlib/thread_local.cryo:151:38  ThreadLocal
    tests/unit       tests/tests/stdlib/thread_local.cryo:51:32  ThreadLocal
    tests/unit       tests/tests/stdlib/thread_local.cryo:76:32  ThreadLocal
    tests/unit       tests/tests/stdlib/thread_local.cryo:92:32  ThreadLocal
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_action.cryo:232:46  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_action.cryo:303:45  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_action.cryo:31:44  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_lens.cryo:195:45  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_lens.cryo:201:45  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_lens.cryo:276:45  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/code_lens.cryo:49:43  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/completion.cryo:246:42  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/completion.cryo:624:46  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/diagnostics.cryo:60:59  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/inactive_regions.cryo:68:13  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/inactive_regions.cryo:93:55  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/lifecycle.cryo:142:49  Array
    tools/CryoLSP    tools/CryoLSP/src/handlers/semantic_tokens.cryo:128:31  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/conv.cryo:235:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/conv.cryo:297:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/conv.cryo:321:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/conv.cryo:387:13  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/conv.cryo:411:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/jsonrpc.cryo:261:52  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/semantic_tokens.cryo:109:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/semantic_tokens.cryo:136:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/semantic_tokens.cryo:169:44  Array
    tools/CryoLSP    tools/CryoLSP/src/protocol/semantic_tokens.cryo:216:44  Array
    tools/CryoLSP    tools/CryoLSP/src/server/diag_render_cache.cryo:49:26  HashMap
    tools/CryoLSP    tools/CryoLSP/src/server/diag_render_cache.cryo:50:26  HashMap
    tools/CryoLSP    tools/CryoLSP/src/server/diag_render_cache.cryo:84:55  Array
    tools/CryoLSP    tools/CryoLSP/src/server/docs.cryo:57:31  HashMap
    tools/CryoLSP    tools/CryoLSP/src/server/line_index.cryo:40:34  Array
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:199:50  Box
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:221:13  Box
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:273:25  HashMap
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:274:25  Array
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:275:25  Array
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:355:24  Array
    tools/CryoLSP    tools/CryoLSP/src/server/session.cryo:362:35  Array
