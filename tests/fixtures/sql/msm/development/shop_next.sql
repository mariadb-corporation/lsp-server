-- An MSM development script: its sections are in sections/, included with
-- SOURCE as msm_plugin's prepare_release expands it.
USE shop;

SOURCE './sections/140_tables.sql'[ln3:ln-2]; -- Remove header and footer

SOURCE './sections/150_routines.sql'[ln3:];

DELIMITER %%
CREATE PROCEDURE restore_roles()
BEGIN
    SOURCE './sections/170_roles.sql';
END%%
DELIMITER ;

SOURCE './sections/180_extra.sql'[0:];
