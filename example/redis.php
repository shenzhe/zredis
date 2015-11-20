<?php
	$h = new zredis();
        $h->connect();
	$h->set("key", "val");
	echo $h->get("key");
