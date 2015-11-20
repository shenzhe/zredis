<?php
	$h = new zredis();
        $h->connect('/tmp/redis.sock');
	$h->set("key", "val");
	echo $h->get("key");
