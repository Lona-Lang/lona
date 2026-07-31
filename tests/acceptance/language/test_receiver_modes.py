from __future__ import annotations

from tests.harness.compiler import CompilerHarness


def _expect_failure_contains(
    compiler: CompilerHarness, name: str, source: str, needles: list[str]
) -> None:
    result = compiler.emit_ir(compiler.write_source(name, source)).expect_failed()
    for needle in needles:
        assert needle in result.stderr, result.describe()


def test_var_method_copies_small_receiver_and_allows_local_mutation(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "var_receiver_small.lo",
        """
        struct Pair {
            set left i32
            set right i32

            var def shifted(step i32) Self {
                self.left = self.left + step
                ret self
            }
        }

        def run() i32 {
            var source = Pair(left = 1, right = 2)
            var result = source.shifted(4)
            ret source.left * 10 + result.left
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="var_receiver_small"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(15)


def test_var_method_copies_const_medium_receiver(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "var_receiver_const_medium.lo",
        """
        struct Triple {
            set a i32
            set b i32
            set c i32

            var def changed() Self {
                self.c = 9
                ret self
            }
        }

        def run() i32 {
            const source Triple = Triple(a = 1, b = 2, c = 3)
            var result = source.changed()
            ret source.c * 10 + result.c
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="var_receiver_const_medium"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(39)


def test_receiver_modes_cover_const_pointer_and_temporary_sources(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "receiver_source_matrix.lo",
        """
        struct Counter {
            set value i32

            def read() i32 {
                ret self.value
            }

            set def bump() {
                self.value = self.value + 1
            }

            var def bumped(step i32) Self {
                self.value = self.value + step
                ret self
            }
        }

        def run() i32 {
            var source = Counter(value = 2)
            const frozen Counter = Counter(value = 4)
            var ptr Counter* = &source
            var readonly Counter const* = &frozen
            const fixed Counter* = &source

            fixed.bump()
            Counter(value = 9).bump()

            var from_ptr = ptr.bumped(5)
            var from_readonly = readonly.bumped(6)
            var from_temporary = Counter(value = 1).bumped(2)

            ret source.read() + frozen.read() + ptr.read() + readonly.read() +
                from_ptr.value + from_readonly.value + from_temporary.value
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="receiver_source_matrix"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(35)


def test_extend_struct_uses_all_receiver_modes(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "extend_struct_modes.lo",
        """
        struct Pair {
            set left i32
            set right i32
        }

        extend Pair {
            def total() i32 {
                ret self.left + self.right
            }

            set def bump() {
                self.left = self.left + 1
            }

            var def shifted(step i32) Self {
                self.right = self.right + step
                ret self
            }
        }

        def run() i32 {
            var source = Pair(left = 2, right = 3)
            source.bump()
            var result = source.shifted(4)
            ret source.total() * 10 + result.total()
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="extend_struct_modes"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(70)


def test_extend_builtin_value_receiver(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "extend_builtin_value.lo",
        """
        extend i32 {
            var def doubled() i32 {
                self = self * 2
                ret self
            }
        }

        var value i32 = 7
        ret value.doubled()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="extend_builtin_value"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(14)


def test_receiver_modes_emit_managed_bitcode(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "receiver_modes_managed.lo",
        """
        struct Counter {
            set value i32

            var def bumped(step i32) Self {
                self.value = self.value + step
                ret self
            }
        }

        extend Counter {
            def read() i32 {
                ret self.value
            }
        }

        def main() i32 {
            var source = Counter(value = 2)
            ret source.bumped(3).read()
        }

        ret main()
        """,
    )
    result, output_path = compiler.emit_managed_bc(
        input_path,
        output_name="receiver-modes-managed.bc",
        target="x86_64-unknown-linux-gnu",
    )
    result.expect_ok()
    assert output_path.read_bytes()[:4] == b"BC\xc0\xde"


def test_type_qualified_calls_expose_receiver_shape(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "qualified_receiver_modes.lo",
        """
        struct Counter {
            set value i32

            def read() i32 {
                ret self.value
            }

            set def bump() {
                self.value = self.value + 1
            }

            var def bumped(step i32) Counter {
                self.value = self.value + step
                ret self
            }
        }

        def run() i32 {
            var source = Counter(value = 2)
            Counter.bump(&source)
            var result = Counter.bumped(source, 4)
            ret Counter.read(&source) * 10 + result.value
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="qualified_receiver_modes"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(37)


def test_method_references_expose_receiver_shape(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "receiver_method_references.lo",
        """
        struct Counter {
            set value i32

            def read() i32 {
                ret self.value
            }

            set def bump() {
                self.value = self.value + 1
            }

            var def bumped(step i32) Self {
                self.value = self.value + step
                ret self
            }
        }

        def run() i32 {
            var read = @Counter.read
            var bump = @Counter.bump
            var bumped = @Counter.bumped
            var source = Counter(value = 2)
            bump(&source)
            var result = bumped(source, 4)
            ret read(&source) * 10 + result.value
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="receiver_method_references"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(37)


def test_large_value_receiver_method_reference_uses_value_abi(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "receiver_method_reference_large.lo",
        """
        struct Big {
            set a i64
            set b i64
            set c i64

            var def changed(step i64) Self {
                self.c = self.c + step
                ret self
            }
        }

        var changed = @Big.changed
        var source = Big(a = 1, b = 2, c = 3)
        var result = changed(source, 4)
        ret cast[i32](source.c * 10 + result.c)
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="receiver_method_reference_large"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(37)


def test_generic_struct_value_receiver_resolves_self(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "generic_struct_value_receiver.lo",
        """
        struct Box[T] {
            set value T

            var def replaced(value T) Self {
                self.value = value
                ret self
            }
        }

        var source = Box[i32](value = 3)
        var result = source.replaced(8)
        ret source.value * 10 + result.value
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="generic_struct_value_receiver"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(38)


def test_generic_value_method_and_method_reference_resolve_self(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "generic_value_method_receiver.lo",
        """
        struct Counter {
            set value i32

            var def replaced[T](ignored T, value i32) Self {
                self.value = value
                ret self
            }
        }

        var source = Counter(value = 3)
        var direct = source.replaced(true, 7)
        var replace_i32 = @Counter.replaced[i32]
        var indirect = replace_i32(source, 0, 9)
        ret source.value * 100 + direct.value * 10 + indirect.value
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="generic_value_method_receiver"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(123)


def test_imported_generic_struct_value_receiver(
    compiler: CompilerHarness,
) -> None:
    compiler.write_source(
        "generic_receiver_import/dep.lo",
        """
        struct Box[T] {
            set value T

            var def replaced(value T) Self {
                self.value = value
                ret self
            }
        }
        """,
    )
    input_path = compiler.write_source(
        "generic_receiver_import/main.lo",
        """
        import dep

        var source = dep.Box[i32](value = 2)
        var direct = source.replaced(5)
        ret source.value * 10 + direct.value
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="generic_receiver_import.exe"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(25)


def test_trait_value_receiver_supports_concrete_and_dyn_calls(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "trait_value_receiver.lo",
        """
        trait Shift {
            var def shifted(step i32) Counter
        }

        struct Counter {
            set value i32
        }

        extend Counter {
            impl Shift {
                var def shifted(step i32) Counter {
                    self.value = self.value + step
                    ret self
                }
            }
        }

        def run() i32 {
            var source = Counter(value = 2)
            var concrete = Shift.shifted(source, 3)
            var view Shift dyn = cast[Shift dyn](&source)
            var dynamic = view.shifted(4)
            const frozen = Counter(value = 5)
            const readonly Shift const dyn = cast[Shift dyn](&frozen)
            var from_readonly = readonly.shifted(1)
            ret source.value * 50 + concrete.value * 10 +
                dynamic.value + from_readonly.value
        }

        ret run()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="trait_value_receiver"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(162)


def test_trait_self_type_resolves_for_concrete_value_receiver_calls(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "trait_value_receiver_self.lo",
        """
        trait Transform {
            var def transformed(step i32) Self
        }

        struct Counter {
            set value i32
        }

        extend Counter {
            impl Transform {
                var def transformed(step i32) Self {
                    self.value = self.value + step
                    ret self
                }
            }
        }

        var source = Counter(value = 3)
        var result = Transform.transformed(source, 4)
        ret source.value * 10 + result.value
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="trait_value_receiver_self"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(37)


def test_trait_self_signature_is_rejected_through_dyn(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "trait_dyn_self_not_object_safe.lo",
        """
        trait Transform {
            var def transformed() Self
        }

        struct Counter {
            value i32
        }

        extend Counter {
            impl Transform {
                var def transformed() Self {
                    ret self
                }
            }
        }

        var source = Counter(value = 3)
        var view Transform dyn = cast[Transform dyn](&source)
        view.transformed()
        """,
    )
    failed = compiler.emit_ir(input_path).expect_failed()
    assert "is not available through `Trait dyn` because its signature mentions `Self`" in failed.stderr


def test_trait_value_receiver_dyn_thunk_handles_indirect_result(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "trait_value_receiver_indirect.lo",
        """
        struct Big {
            set a i64
            set b i64
            set c i64
        }

        trait Adjust {
            var def changed(step i64) Big
        }

        extend Big {
            impl Adjust {
                var def changed(step i64) Big {
                    self.c = self.c + step
                    ret self
                }
            }
        }

        var source = Big(a = 1, b = 2, c = 3)
        var view Adjust dyn = cast[Adjust dyn](&source)
        var result = view.changed(4)
        ret cast[i32](source.c * 10 + result.c)
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="trait_value_receiver_indirect"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(37)


def test_extension_import_is_visible_and_uses_stable_receiver_abi(
    compiler: CompilerHarness,
) -> None:
    compiler.write_source(
        "receiver_extension_import/dep.lo",
        """
        struct Point {
            set value i32
        }

        extend Point {
            var def plus(step i32) Self {
                self.value = self.value + step
                ret self
            }
        }
        """,
    )
    input_path = compiler.write_source(
        "receiver_extension_import/main.lo",
        """
        import dep

        var source = dep.Point(value = 3)
        var result = source.plus(4)
        ret source.value * 10 + result.value
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="receiver_extension_import.exe"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(37)


def test_local_extension_can_target_imported_type(
    compiler: CompilerHarness,
) -> None:
    compiler.write_source(
        "receiver_extension_foreign/dep.lo",
        """
        struct Point {
            set value i32
        }
        """,
    )
    input_path = compiler.write_source(
        "receiver_extension_foreign/main.lo",
        """
        import dep

        extend dep.Point {
            def read() i32 {
                ret self.value
            }
        }

        ret dep.Point(value = 9).read()
        """,
    )
    build_result, exe_path = compiler.build_system_executable(
        input_path, output_name="receiver_extension_foreign.exe"
    )
    build_result.expect_ok()
    compiler.run_executable(exe_path).expect_exit_code(9)


def test_receiver_and_extend_ast_json_exposes_stable_modes(
    compiler: CompilerHarness,
) -> None:
    input_path = compiler.write_source(
        "receiver_extend_ast.lo",
        """
        struct Point {
            value i32

            def read() i32 {
                ret self.value
            }
        }

        extend Point {
            set def reset() {
            }

            var def copied() Self {
                ret self
            }
        }
        """,
    )
    json_out = compiler.emit_json(input_path).expect_ok().stdout
    for needle in [
        '"type": "ExtendDecl"',
        '"targetType": "Point"',
        '"receiverMode": "get"',
        '"receiverMode": "set"',
        '"receiverMode": "var"',
    ]:
        assert needle in json_out


def test_receiver_modifier_and_shape_diagnostics(
    compiler: CompilerHarness,
) -> None:
    cases = [
        (
            "top_level_var_def_bad.lo",
            """
            var def wrong() {
            }
            """,
            ["`var def` is only valid on methods"],
        ),
        (
            "duplicate_receiver_mode_bad.lo",
            """
            struct Demo {
                def value() i32 {
                    ret 1
                }

                var def value() i32 {
                    ret 2
                }
            }
            """,
            ["duplicate method `value`"],
        ),
        (
            "set_const_receiver_bad.lo",
            """
            struct Counter {
                set value i32

                set def bump() {
                    self.value = self.value + 1
                }
            }

            const value = Counter(value = 1)
            value.bump()
            """,
            ["set method `bump` requires a writable receiver"],
        ),
        (
            "set_const_pointer_receiver_bad.lo",
            """
            struct Counter {
                set value i32

                set def bump() {
                    self.value = self.value + 1
                }
            }

            const value = Counter(value = 1)
            var ptr Counter const* = &value
            ptr.bump()
            """,
            ["set method `bump` requires a writable receiver"],
        ),
        (
            "value_receiver_const_field_bad.lo",
            """
            struct Wrapped {
                frozen i32 const*

                var def changed() Self {
                    *self.frozen = 2
                    ret self
                }
            }
            """,
            ["assignment target contains read-only storage"],
        ),
        (
            "qualified_value_receiver_pointer_bad.lo",
            """
            struct Counter {
                value i32

                var def copied() Self {
                    ret self
                }
            }

            var source = Counter(value = 1)
            Counter.copied(&source)
            """,
            ["var method `copied` requires an explicit value receiver"],
        ),
        (
            "qualified_borrowed_receiver_value_bad.lo",
            """
            struct Counter {
                value i32

                def read() i32 {
                    ret self.value
                }
            }

            var source = Counter(value = 1)
            Counter.read(source)
            """,
            ["borrowed method `read` requires an explicit self pointer"],
        ),
        (
            "generic_nongeneric_method_name_conflict_bad.lo",
            """
            struct Demo {
                def value() i32 {
                    ret 1
                }

                var def value[T](ignored T) i32 {
                    ret 2
                }
            }
            """,
            ["duplicate method `value`"],
        ),
    ]
    for name, source, needles in cases:
        _expect_failure_contains(compiler, name, source, needles)


def test_invalid_receiver_and_extend_forms_use_regular_syntax_errors(
    compiler: CompilerHarness,
) -> None:
    cases = [
        (
            "receiver_modifier_combination_bad.lo",
            """
            struct Demo {
                set var def wrong() {
                }
            }
            """,
        ),
        (
            "extension_field_syntax_bad.lo",
            """
            extend i32 {
                value i32
            }
            """,
        ),
        (
            "extension_nested_syntax_bad.lo",
            """
            extend i32 {
                extend i32 {
                }
            }
            """,
        ),
    ]
    for name, source in cases:
        _expect_failure_contains(compiler, name, source, ["syntax error"])


def test_generic_extend_rejects_ordinary_extension_methods(
    compiler: CompilerHarness,
) -> None:
    _expect_failure_contains(
        compiler,
        "extension_generic_method_bad.lo",
        """
        struct Box[T] {
            value T
        }

        extend[T] Box[T] {
            def get() T {
                ret self.value
            }
        }
        """,
        ["generic extend declarations currently only support trait impl blocks"],
    )


def test_extension_preserves_external_field_access_boundary(
    compiler: CompilerHarness,
) -> None:
    for mode in ["set", "var"]:
        _expect_failure_contains(
            compiler,
            f"extension_private_field_{mode}_bad.lo",
            f"""
            struct Point {{
                value i32
            }}

            extend Point {{
                {mode} def rewrite() {{
                    self.value = 2
                }}
            }}
            """,
            ["assignment target contains read-only storage"],
        )


def test_extension_declaration_diagnostics(
    compiler: CompilerHarness,
) -> None:
    cases = [
        (
            "extension_bodyless_bad.lo",
            """
            extend i32 {
                def read() i32
            }
            """,
            ["extension method `read` must have a body"],
        ),
        (
            "extension_pointer_target_bad.lo",
            """
            extend i32* {
                def read() i32 {
                    ret 0
                }
            }
            """,
            ["unsupported extend target `i32*`"],
        ),
        (
            "extension_dyn_target_bad.lo",
            """
            trait Show {
                def show() i32
            }

            extend Show dyn {
                def read() i32 {
                    ret 0
                }
            }
            """,
            ["unsupported extend target `Show dyn`"],
        ),
        (
            "extension_bare_generic_target_bad.lo",
            """
            struct Box[T] {
                value T
            }

            extend Box {
                def read() i32 {
                    ret 0
                }
            }
            """,
            ["generic type template `Box` requires explicit `[...]` type arguments"],
        ),
        (
            "old_extension_syntax_bad.lo",
            """
            def i32.read() i32 {
                ret self
            }
            """,
            ["syntax error"],
        ),
    ]
    for name, source, needles in cases:
        _expect_failure_contains(compiler, name, source, needles)


def test_local_extension_method_names_conflict_across_receiver_modes(
    compiler: CompilerHarness,
) -> None:
    _expect_failure_contains(
        compiler,
        "extension_local_conflict_bad.lo",
        """
        extend i32 {
            def value() i32 {
                ret *self
            }
        }

        extend i32 {
            var def value() i32 {
                ret self
            }
        }
        """,
        [
            "visible extension method conflict for `i32.value`",
            "(`get`)",
            "(`var`)",
        ],
    )
