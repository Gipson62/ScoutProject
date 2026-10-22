# modes/capture.py: tap a point to claim it, hold it uncontested to own it.
#
# This is the capture half of the camp's domination mode (docs/DESIGN.md,
# "Domination mode for the camp"), on its own: a tap lights the point in the
# team's colour and starts a countdown, a rival tap restarts that countdown, and
# the point changes hands only when the countdown runs out.
#
# Determinism rules every mode follows: time only through game.now, randomness
# only through game.random, and no state outside game.data, team.data and
# point.data. A mode that follows them replays to exactly the same state.

from scoutgame import on_start, on_tag, minutes


@on_start
def setup(game):
    for point in game.points:
        point.owner = None
        point.data["claimant"] = None
        point.data["timer"] = None
        point.light("white", "solid")  # nobody holds it yet


@on_tag()
def tagged(game, point, tag):
    team = tag.team
    if team is None:
        point.beep("deny")  # a wristband nobody enrolled
        return

    owner = point.owner
    claimant = point.data["claimant"]
    holds = owner is not None and owner.name == team.name
    claims = claimant is not None and claimant.name == team.name
    if holds or claims:
        point.beep("deny")  # already yours, or already counting down for you
        return

    if point.data["timer"] is not None:
        game.cancel(point.data["timer"])  # a rival tap restarts the countdown

    point.data["claimant"] = team
    point.light(team.colour, "pulse")  # pulse = claimed, not yet owned
    point.beep("ok")
    point.data["timer"] = game.after(minutes(game.p.capture_minutes), confirm, point)


def confirm(game, point):
    point.owner = point.data["claimant"]
    point.data["claimant"] = None
    point.data["timer"] = None
    point.light(point.owner.colour, "solid")  # solid = owned
    game.announce(point.owner.name + " holds " + point.name, to="all")
