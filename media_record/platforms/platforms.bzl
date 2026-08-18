"""Platform build helpers for media_record.

Provides:
  - config_setting_and_platform(): create config_setting + platform pairs.
  - media_record_select(): convenience select() for consumers.
"""

def config_setting_and_platform(name, constraint_values):
    native.config_setting(
        name = name,
        constraint_values = constraint_values,
    )
    native.platform(
        name = name + "_platform",
        constraint_values = constraint_values,
    )

def media_record_select(select_map):
    return select(select_map)
