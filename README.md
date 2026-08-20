# "F*ck Ugly Trees" Association - AzerothCore module

## Description

Apply Tree of Life passive healing bonuses to Restoration Druids without having to shapeshift into an ugly tree.

## Features

When a Druid has the Tree of Life talent, this module applies the form-specific passive auras directly in humanoid form:

- raid-wide +6% healing received from the core talent (polymorph immunity not included)
- increased armor and spirit-to-bonus-healing from "Improved Tree of Life"
- healing increase from "Master Shapeshifter"

Auras are removed when shapeshifting into any form and re-applied when returning to back humanoid.

All bonuses are individually toggleable and have adjustable multipliers.  
By default every multiplier is 1.0, except armor increase from "Improved Tree of Life", which is set to 0.33.

## Installation

1. Clone this repo as `modules/mod-futa` in AzerothCore source tree
2. Run `cmake`

> [!NOTE]
> The module uses custom `spell_dbc` entries that ***might*** conflict with other existing modules or future AzerothCore updates.  

| ID    | Spell                        |
|-------|------------------------------|
| 96961 | Tree of Life                 |
| 96962 | Master Shapeshifter Rank 1   |
| 96963 | Master Shapeshifter Rank 2   |
| 96964 | Improved Tree of Life Rank 1 |
| 96965 | Improved Tree of Life Rank 2 |
| 96966 | Improved Tree of Life Rank 3 |
| 96967 | Tree of Life Healing Boost   |

If there is a conflict, the DB will throw an error. Manually adjust IDs in `futa_spell_dbc.sql` and `futa.cpp` if needed.

## Configuration

See `conf/mod_futa.conf.dist`.

