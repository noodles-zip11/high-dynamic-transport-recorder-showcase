def test_environment_smoke() -> None:
    assert 2 + 2 == 4


def test_desktop_dependencies_are_importable() -> None:
    import numpy
    import pyqtgraph
    import PySide6

    assert numpy.__version__
    assert pyqtgraph.__version__
    assert PySide6.__version__
