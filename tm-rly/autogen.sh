
# $Id: autogen.sh 161 2006-12-19 02:35:21Z gregor $

if which aclocal-1.11 > /dev/null 2>/dev/null; then
	ACLOCAL=aclocal-1.11
else 
	echo "Could not found aclocal-1.9 or aclocal19. Exiting"
	exit
fi

if which  automake-1.11 > /dev/null 2>/dev/null; then
	AM=automake-1.11
else 
	echo "Could not found automake-1.9 or automake19. Exiting"
	exit
fi


#aclocal-1.9 && autoheader && autoconf && automake-1.9 --add-missing --copy

# autoheader  depends on aclocal
# autoconf    depends on aclocal
# configure depends on everything
# no other dependencies
${ACLOCAL} \
	&& autoheader \
	&&  autoconf \
	&& ${AM} --add-missing --copy
