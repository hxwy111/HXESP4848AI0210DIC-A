idf_component_register(
  SRCS "touch_bsp.c"

  PRIV_REQUIRES
  i2c_bsp
  board_config driver

  INCLUDE_DIRS ".")

## 组件依赖由 PRIV_REQUIRES 指定
