/* Drehung der Oberflaeche vom Compositor lesen. */
#ifndef IMIRA_ORIENT_H_
#define IMIRA_ORIENT_H_

/* Verbindet sich mit dem Sitzungsbus. 0 = geht, -1 = geht nicht. */
int orient_open(void);
/* Winkel der Oberflaeche in Grad (0/90/180/270) oder -1, wenn unbekannt. */
int orient_angle(void);
void orient_close(void);

#endif
