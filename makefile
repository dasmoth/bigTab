kentSrc = ../..
A = tabToBigTab
include $(kentSrc)/inc/userApp.mk
L += -lm -lz ${SOCKETLIB}
