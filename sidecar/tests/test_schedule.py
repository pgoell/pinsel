import pytest

from pinsel_sidecar.schedule import inpaint_steps


def test_full_strength_runs_every_step():
    assert inpaint_steps(30, 1.0) == (30, 30)


def test_runs_the_last_steps_times_strength():
    assert inpaint_steps(30, 0.5) == (30, 15)


def test_a_low_strength_asks_for_more_steps_so_one_runs():
    assert inpaint_steps(30, 0.03) == (34, 1)
    assert inpaint_steps(1, 0.01) == (100, 1)


@pytest.mark.parametrize("strength", [0, -0.5, 1.5, float("nan"), 1e-9])
def test_refuses_a_strength_it_cannot_run(strength):
    with pytest.raises(ValueError):
        inpaint_steps(30, strength)
