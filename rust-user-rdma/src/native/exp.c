#include "../bindings.h"

int bd_ibv_exp_query_device(struct ibv_context *context,
                                       struct ibv_exp_device_attr *attr) {
  return ibv_exp_query_device(context, attr);
}
