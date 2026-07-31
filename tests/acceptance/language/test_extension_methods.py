from __future__ import annotations

from tests.acceptance.language._syntax_helpers import _emit_ir, _expect_ir_failure
from tests.harness import assert_contains, assert_not_contains, assert_regex
from tests.harness.compiler import CompilerHarness


def test_extension_methods_support_all_receiver_modes_and_const_matching(
    compiler: CompilerHarness,
) -> None:
    ir = _emit_ir(
        compiler,
        "extension_receiver_modes_ok.lo",
        """
        extend i32 {
            var def doubled() i32 {
                self = self * 2
                ret self
            }

            def peek() i32 {
                ret *self
            }

            set def add(step i32) {
                *self = *self + step
            }
        }

        def main() i32 {
            var value i32 = 4
            const frozen i32 = 7
            value.add(3)
            if value.peek() != 7 {
                ret 1
            }
            if frozen.doubled() != 14 {
                ret 2
            }
            ret 0
        }
        """,
    )
    assert_regex(
        ir,
        r"call i32 @.*\.__extend__\.var\.doubled\(i32 ",
        label="extension value receiver ir",
    )
    assert_regex(
        ir,
        r"call i32 @.*\.__extend__\.get\.peek\(ptr ",
        label="extension readonly borrowed ir",
    )
    assert_regex(
        ir,
        r"call void @.*\.__extend__\.set\.add\(ptr ",
        label="extension writable borrowed ir",
    )


def test_extension_methods_materialize_temporary_borrowed_receivers(
    compiler: CompilerHarness,
) -> None:
    ir = _emit_ir(
        compiler,
        "extension_struct_temporary_ok.lo",
        """
        struct Pair {
            left i32
            right i32
        }

        extend Pair {
            def sum() i32 {
                ret self.left + self.right
            }
        }

        def main() i32 {
            ret Pair(left = 1, right = 2).sum()
        }
        """,
    )
    assert_regex(
        ir,
        r"call i32 @.*\.__extend__\.get\.sum\(ptr ",
        label="extension temporary borrowed ir",
    )


def test_pointer_dot_call_dereferences_before_value_extension_binding(
    compiler: CompilerHarness,
) -> None:
    ir = _emit_ir(
        compiler,
        "extension_pointer_value_receiver_ok.lo",
        """
        extend i32 {
            var def doubled() i32 {
                ret self * 2
            }
        }

        def main() i32 {
            var value i32 = 7
            var ptr i32* = &value
            ret ptr.doubled()
        }
        """,
    )
    assert_regex(
        ir,
        r"call i32 @.*\.__extend__\.var\.doubled\(i32 ",
        label="pointer value extension ir",
    )


def test_scalar_literals_materialize_readonly_borrowed_extension_receivers(
    compiler: CompilerHarness,
) -> None:
    ir = _emit_ir(
        compiler,
        "extension_literal_borrowed_materialization_ok.lo",
        """
        extend i32 {
            def peek() i32 {
                ret *self
            }
        }

        def main() i32 {
            ret 1.peek()
        }
        """,
    )
    assert_regex(
        ir,
        r"call i32 @.*\.__extend__\.get\.peek\(ptr ",
        label="literal borrowed extension ir",
    )


def test_extension_methods_follow_direct_import_visibility_only(
    compiler: CompilerHarness,
) -> None:
    compiler.write_source(
        "extension_import_visibility/dep_a.lo",
        """
        extend i32 {
            var def extra() i32 {
                ret self + 6
            }
        }
        """,
    )
    compiler.write_source(
        "extension_import_visibility/dep_b.lo",
        """
        import dep_a

        def sentinel() i32 {
            ret 1
        }
        """,
    )

    hidden_main = compiler.write_source(
        "extension_import_visibility/main_hidden.lo",
        """
        import dep_b

        def main() i32 {
            ret 1.extra()
        }
        """,
    )
    hidden = compiler.emit_ir(hidden_main).expect_failed()
    assert_contains(
        hidden.stderr,
        "unknown member `i32.extra`",
        label="extension indirect import diagnostic",
    )

    direct_main = compiler.write_source(
        "extension_import_visibility/main_direct.lo",
        """
        import dep_a

        def main() i32 {
            ret 1.extra()
        }
        """,
    )
    direct_ir = compiler.emit_ir(direct_main).expect_ok().stdout
    assert_regex(
        direct_ir,
        r"call i32 @dep_a\..*\.__extend__\.var\.extra\(i32 ",
        label="extension direct import ir",
    )


def test_extension_import_conflicts_are_ambiguous_and_inherent_methods_win(
    compiler: CompilerHarness,
) -> None:
    compiler.write_source(
        "extension_conflicts/dep_a.lo",
        """
        extend i32 {
            var def extra() i32 {
                ret self + 1
            }
        }
        """,
    )
    compiler.write_source(
        "extension_conflicts/dep_b.lo",
        """
        extend i32 {
            def extra() i32 {
                ret *self + 2
            }
        }
        """,
    )
    import_conflict = compiler.write_source(
        "extension_conflicts/main_import_conflict.lo",
        """
        import dep_a
        import dep_b

        ret 1.extra()
        """,
    )
    import_failed = compiler.emit_ir(import_conflict).expect_failed()
    for needle in [
        "visible extension method conflict for `i32.extra`",
        "dep_a",
        "dep_b",
        "(`var`)",
        "(`get`)",
    ]:
        assert_contains(
            import_failed.stderr,
            needle,
            label="extension import conflict diagnostic",
        )

    inherent_ir = _emit_ir(
        compiler,
        "extension_inherent_precedence.lo",
        """
        struct Point {
            x i32

            def len() i32 {
                ret self.x
            }
        }

        extend Point {
            def len() i32 {
                ret self.x + 1
            }
        }

        def main() i32 {
            ret Point(x = 4).len()
        }
        """,
    )
    assert_regex(
        inherent_ir,
        r"call i32 @.*Point\.len\.__receiver_get\(ptr ",
        label="inherent precedence ir",
    )
    assert_not_contains(
        inherent_ir,
        "call i32 @extension_inherent_precedence.extension_5finherent_5fprecedence_2ePoint.__extend__.get.len",
        label="inherent precedence ir",
    )


def test_extension_method_bare_selectors_are_rejected(
    compiler: CompilerHarness,
) -> None:
    _expect_ir_failure(
        compiler,
        "extension_selector_bad.lo",
        """
        extend i32 {
            var def kind() i32 {
                ret self
            }
        }

        def main() i32 {
            1.kind
            ret 0
        }
        """,
        ["extension method `kind` can only be used as a direct call callee"],
    )


def test_extension_methods_do_not_participate_in_generic_bound_lookup(
    compiler: CompilerHarness,
) -> None:
    _expect_ir_failure(
        compiler,
        "extension_generic_bound_lookup_bad.lo",
        """
        trait Hash {
            def hash() i32
        }

        struct Point {
            value i32

            def hash() i32 {
                ret self.value
            }
        }

        extend Point {
            impl Hash {
                def hash() i32 {
                    ret self.hash()
                }
            }
        }

        extend Point {
            def extra() i32 {
                ret self.value + 1
            }
        }

        def use[T Hash](value T) i32 {
            ret value.extra()
        }
        """,
        [
            "generic parameter `T` does not provide member `extra` through bound `Hash`",
            "Bounded generic parameters only allow methods provided by bound `Hash`, such as `value.method()`.",
        ],
    )
