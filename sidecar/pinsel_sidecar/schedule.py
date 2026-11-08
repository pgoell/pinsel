"""How many steps an inpaint asks diffusers for, and how many it runs."""

import math

# SDXL trains on 1000 timesteps; a schedule cannot be finer.
MAX_STEPS = 1000


def inpaint_steps(steps: int, strength: float) -> tuple[int, int]:
    """Returns the steps to ask for and the steps that run.

    diffusers runs only the last int(steps * strength) steps of the schedule,
    and refuses to run none, so a low strength asks for more steps.
    """
    if not 0 < strength <= 1:
        raise ValueError(f"strength {strength} is not in (0, 1]")
    asked = max(steps, math.ceil(1 / strength))
    if int(asked * strength) < 1:
        asked += 1
    if asked > MAX_STEPS:
        raise ValueError(f"strength {strength} needs more than {MAX_STEPS} steps")
    return asked, min(asked, int(asked * strength))
