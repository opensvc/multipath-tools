void init_handler_callbacks(void)
{
	set_handler_callback(VRB_LIST | Q1_PATHS, HANDLER(cli_list_paths));
	set_handler_callback(VRB_LIST | Q1_PATHS | Q2_FMT, HANDLER(cli_list_paths_fmt));
	set_handler_callback(VRB_LIST | Q1_PATHS | Q2_RAW | Q3_FMT,
			     HANDLER(cli_list_paths_raw));
	set_handler_callback(VRB_LIST | Q1_PATH, HANDLER(cli_list_path));
	set_handler_callback(VRB_LIST | Q1_MAPS, HANDLER(cli_list_maps));
	set_handler_callback(VRB_LIST | Q1_STATUS, HANDLER(cli_list_status));
	set_unlocked_handler_callback(VRB_LIST | Q1_DAEMON, HANDLER(cli_list_daemon));
	set_handler_callback(VRB_LIST | Q1_MAPS | Q2_STATUS,
			     HANDLER(cli_list_maps_status));
	set_handler_callback(VRB_LIST | Q1_MAPS | Q2_STATS,
			     HANDLER(cli_list_maps_stats));
	set_handler_callback(VRB_LIST | Q1_MAPS | Q2_FMT, HANDLER(cli_list_maps_fmt));
	set_handler_callback(VRB_LIST | Q1_MAPS | Q2_RAW | Q3_FMT,
			     HANDLER(cli_list_maps_raw));
	set_handler_callback(VRB_LIST | Q1_MAPS | Q2_TOPOLOGY,
			     HANDLER(cli_list_maps_topology));
	set_handler_callback(VRB_LIST | Q1_TOPOLOGY, HANDLER(cli_list_maps_topology));
	set_handler_callback(VRB_LIST | Q1_MAPS | Q2_JSON, HANDLER(cli_list_maps_json));
	set_handler_callback(VRB_LIST | Q1_MAP | Q2_TOPOLOGY,
			     HANDLER(cli_list_map_topology));
	set_handler_callback(VRB_LIST | Q1_MAP | Q2_FMT, HANDLER(cli_list_map_fmt));
	set_handler_callback(VRB_LIST | Q1_MAP | Q2_RAW | Q3_FMT,
			     HANDLER(cli_list_map_fmt));
	set_handler_callback(VRB_LIST | Q1_MAP | Q2_JSON, HANDLER(cli_list_map_json));
	set_handler_callback(VRB_LIST | Q1_CONFIG | Q2_LOCAL,
			     HANDLER(cli_list_config_local));
	set_handler_callback(VRB_LIST | Q1_CONFIG, HANDLER(cli_list_config));
	set_handler_callback(VRB_LIST | Q1_BLACKLIST, HANDLER(cli_list_blacklist));
	set_handler_callback(VRB_LIST | Q1_DEVICES, HANDLER(cli_list_devices));
	set_handler_callback(VRB_LIST | Q1_WILDCARDS, HANDLER(cli_list_wildcards));
	set_handler_callback(VRB_RESET | Q1_MAPS | Q2_STATS,
			     HANDLER(cli_reset_maps_stats));
	set_handler_callback(VRB_RESET | Q1_MAP | Q2_STATS,
			     HANDLER(cli_reset_map_stats));
	set_handler_callback(VRB_ADD | Q1_PATH, HANDLER(cli_add_path));
	set_handler_callback(VRB_DEL | Q1_PATH, HANDLER(cli_del_path));
	set_handler_callback(VRB_ADD | Q1_MAP, HANDLER(cli_add_map));
	set_handler_callback(VRB_DEL | Q1_MAP, HANDLER(cli_del_map));
	set_handler_callback(VRB_DEL | Q1_MAPS, HANDLER(cli_del_maps));
	set_handler_callback(VRB_SWITCH | Q1_MAP | Q2_GROUP, HANDLER(cli_switch_group));
	set_unlocked_handler_callback(VRB_RECONFIGURE, HANDLER(cli_reconfigure));
	set_unlocked_handler_callback(VRB_RECONFIGURE | Q1_ALL,
				      HANDLER(cli_reconfigure_all));
	set_handler_callback(VRB_SUSPEND | Q1_MAP, HANDLER(cli_suspend));
	set_handler_callback(VRB_RESUME | Q1_MAP, HANDLER(cli_resume));
	set_handler_callback(VRB_RESIZE | Q1_MAP, HANDLER(cli_resize));
	set_handler_callback(VRB_RELOAD | Q1_MAP, HANDLER(cli_reload));
	set_handler_callback(VRB_RESET | Q1_MAP, HANDLER(cli_reassign));
	set_handler_callback(VRB_REINSTATE | Q1_PATH, HANDLER(cli_reinstate));
	set_handler_callback(VRB_FAIL | Q1_PATH, HANDLER(cli_fail));
	set_handler_callback(VRB_DISABLEQ | Q1_MAP, HANDLER(cli_disable_queueing));
	set_handler_callback(VRB_RESTOREQ | Q1_MAP, HANDLER(cli_restore_queueing));
	set_handler_callback(VRB_DISABLEQ | Q1_MAPS, HANDLER(cli_disable_all_queueing));
	set_handler_callback(VRB_RESTOREQ | Q1_MAPS, HANDLER(cli_restore_all_queueing));
	set_unlocked_handler_callback(VRB_QUIT, HANDLER(cli_quit));
	set_unlocked_handler_callback(VRB_SHUTDOWN, HANDLER(cli_shutdown));
	set_handler_callback(VRB_GETPRSTATUS | Q1_MAP, HANDLER(cli_getprstatus));
	set_handler_callback(VRB_SETPRSTATUS | Q1_MAP, HANDLER(cli_setprstatus));
	set_handler_callback(VRB_SETPRSTATUS | Q1_MAP | Q2_PATHLIST,
			     HANDLER(cli_setprstatus_list));
	set_handler_callback(VRB_UNSETPRSTATUS | Q1_MAP, HANDLER(cli_unsetprstatus));
	set_handler_callback(VRB_FORCEQ | Q1_DAEMON, HANDLER(cli_force_no_daemon_q));
	set_handler_callback(VRB_RESTOREQ | Q1_DAEMON, HANDLER(cli_restore_no_daemon_q));
	set_handler_callback(VRB_GETPRKEY | Q1_MAP, HANDLER(cli_getprkey));
	set_handler_callback(VRB_SETPRKEY | Q1_MAP | Q2_KEY, HANDLER(cli_setprkey));
	set_handler_callback(VRB_UNSETPRKEY | Q1_MAP, HANDLER(cli_unsetprkey));
	set_handler_callback(VRB_SETMARGINAL | Q1_PATH, HANDLER(cli_set_marginal));
	set_handler_callback(VRB_UNSETMARGINAL | Q1_PATH, HANDLER(cli_unset_marginal));
	set_handler_callback(VRB_UNSETMARGINAL | Q1_MAP,
			     HANDLER(cli_unset_all_marginal));
	set_handler_callback(VRB_GETPRHOLD | Q1_MAP, HANDLER(cli_getprhold));
	set_handler_callback(VRB_SETPRHOLD | Q1_MAP, HANDLER(cli_setprhold));
	set_handler_callback(VRB_UNSETPRHOLD | Q1_MAP, HANDLER(cli_unsetprhold));
}
