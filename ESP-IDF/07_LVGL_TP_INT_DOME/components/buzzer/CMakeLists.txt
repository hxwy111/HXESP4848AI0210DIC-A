idf_component_register(
  SRCS "buzzer.c" 
  PRIV_REQUIRES driver board_config
  i2c_bsp
  INCLUDE_DIRS ".")
