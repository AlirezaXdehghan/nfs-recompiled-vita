# Winsock 1 uses a different order for ordinals 10–12 than Winsock 2.
from .ws2_32 import ord_names as ws2_ord_names

ord_names = dict(ws2_ord_names)
ord_names.update({
    10: b'inet_addr',
    11: b'inet_ntoa',
    12: b'ioctlsocket',
})
