from TextTools.TextTools import *
import bootstrap # the hand-coded parser
import operator, strop as string 

def err( value ):
	print value

class _BaseGenerator:
	'''
	Class providing the functions required to turn a 
	parse tree as generated by the bootstrap parser into
	a new set of parser tuples. I.e a parser generator :)
	Effectively this is the bootstrap generator.
	'''
	def __init__( self, syntaxstring = bootstrap.declaration, parserelement = 'declarationset' ):
		'''
		Turn syntaxstring into a parsetree using
		the bootstrap module's parse command
		'''
		# should do some error checking in here :)
		self.syntaxstring = syntaxstring
		self.parsetree = bootstrap.parse( syntaxstring, parserelement )[1][0] # the child list
		self.nameset = []
		self.tupleset = []
	def stringval( self, tuple ):
		'''
		Return the string value for a parse-result tuple
		'''
		return self.syntaxstring[ tuple[1]:tuple[2] ]
	def build( self, prebuiltnodes=() ):
		'''
		Build a new parsing table from the syntax string.
		New parsers may be accessed using the parserbyname method.

		The pre-built nodes are parsing tables for inclusion in the grammar
		Added version 1.0.1 to provide greater extensibility.
		'''
		# first register all declared names to reserve their indicies
		#if self.__class__.__name__ == 'Generator':
		#	import pdb
		#	pdb.set_trace()
		for key, value in prebuiltnodes:
			self.nameset.append( key )
			self.tupleset.append( value )
		for decl in self.parsetree[3]:
			#print decl
			name = self.stringval( decl[3][0] )
			self.nameset.append( name )
			self.tupleset.append( None)
		#print 'Declared names:',self.nameset
		for i in range( len( self.nameset)):
			#print '''Processing declaration %s '''% self.nameset[i]
			dataset = self.group( ('group',1,2, self.parsetree[3][i][3][1:]), self )
			if dataset:
				self.tupleset[i] = tuple( dataset)
	def parserbyname( self, name ):
		'''
		Retrieve a single parsing tuple by its production name
		'''
		try:
			return self.tupleset[ self.nameset.index( name ) ]
		except ValueError:
			print '''Could not find parser tuple of name''', name
			return ()
	def allparsers (self):
		'''
		Return a list of (productionname, parsingtuple) values
		suitable for passing to another generator as its pre-calculated
		set of parsing tuples. (See method build)
		'''
		returnvalue = []
		for i in range(len( self.nameset)):
			returnvalue.append (  (self.nameset[i],self.tupleset[i]) )
		return returnvalue
	### Actual processing functions...
	def element_token( self, eltup, genobj, reportname=None ):
		# Determine the type of element
		# Descry the various options for the element
		negative = optional = repeating = element = None
		for data in eltup[3]:
			if data[0] == 'negpos_indicator':
				if genobj.stringval ( data ) == '-':
					negative = 1
			elif data[0] == 'occurence_indicator':
				data = genobj.stringval ( data )
				if data == '*':
					optional = 1
					repeating = 1
				elif data == '+':
					repeating = 1
				elif data == '?':
					optional = 1
				else:
					err( 'Unknown occurence indicator '+ data )
			else:
				element = data
		# call the appropriate handler
		try:
			return getattr( self, element [0])( element, genobj, negative, repeating, optional)
		except AttributeError,x:
			err( '''Didn't find handler for element type %s, parser build aborted'''%element [0])
			raise x
				
	def group( self, els, genobj, negative= None, repeating=None, optional = None, reportname=None):
		'''
		Determine what type of group we're dealing with and determine what
		function to call, then call it.
		'''
		groupset = els[3]
		# groupset is an element_token followed by a possible added_token
		if groupset:
			els = []
			els.append( groupset[0] )
			if len(groupset) > 1:
				els[len(els):] = groupset[1][3]
				gtype = groupset[1][0]
				if gtype == 'seq_added_token':
					return self.seq( els, genobj, negative, repeating, optional, reportname )
				elif gtype == 'fo_added_token':
					return self.fo( els, genobj, negative, repeating, optional, reportname )
				else:
					err( '''An as-yet undefined group type was used! %s'''%gtype )
			else: # default "sequence" of one... could do more work and make it process the results specifically, but that's optimisation ;)
				return self.seq( els, genobj, negative, repeating, optional, None )
		else:
			return []
		
				
	def seq( self, els, genobj, negative= None, repeating=None, optional = None, reportname=None ):
		elset = map( self.element_token, els, [genobj]*len( els) )
		elset = reduce( operator.add, elset )
		if negative:
			if repeating:
				if optional:
					return [(None, SubTable, (( None, SubTable,( (None, SubTable, tuple( elset), 2,1), (None, Fail, Here),(None,Skip,1) ), 2,1 ), ( None, EOF, Here, -1,1 ), ), ), ]
				else: # not optional
					return [(None, SubTable, (( None, SubTable,( (None, SubTable, tuple( elset), 2,1), (None, Fail, Here),(None,Skip,1) )), ( None, SubTable,( (None, SubTable, tuple( elset), 2,1), (None, Fail, Here),(None,Skip,1) ), 2,1 ), ( None, EOF, Here, -1,1 ), ), ), ]
			else: # single
				if optional:
					return [ (None, SubTable, ( (None, SubTable, tuple( elset), 2,1), (None, Fail, Here), (None, Skip, 1) ),1,1) ]
				else: # not optional
					return [ (None, SubTable, ( (None, SubTable, tuple( elset), 2,1), (None, Fail, Here), (None, Skip, 1) )) ]
		else: # positive
			if repeating:
				if optional:
					return [ (None, SubTable, tuple( elset), 1,0) ]
				else: # not optional
					
					return [ (None, SubTable, tuple( elset)), (None, SubTable, tuple( elset), 1,0)  ]
			else: # single
				if optional:
					return [ (None, SubTable, tuple( elset), 1,1) ]
				else: # not optional
					return [ (None, SubTable, tuple( elset)) ]
			
	def fo( self, els, genobj, negative= None, repeating=None, optional = None, reportname=None ):
		elset = map( self.element_token, els, [genobj]*len( els) )
		elset = reduce( operator.add, elset )
		elset = []
		for el in els:
			dataset = self.element_token( el, genobj )
			if len( dataset) == 1 and len(dataset[0]) == 3: # we can alter the jump states with impunity
				elset.append( dataset[0] )
			else: # for now I'm eating the inefficiency and doing an extra SubTable for all elements to allow for easy calculation of jumps within the FO group
				elset.append(  (None, SubTable, tuple( dataset ))  )
		if negative:
			# all negative FO's have the meaning "a positive, single, non-optional FO not matching"
			# the flags modify how failure and continuation are handled in that case, so they can use
			# the same procset.
			# Note: Negative FO groups are _very_ heavy, they have normally about 4 subtable calls
			# guess we'll find out how well mxTextTools handles recursive tables :)
			procset = []
			for i in range( len( elset) -1): # note that we have to treat last el specially
				ival = elset[i] + (1,len(elset)-i)
				procset.append( ival ) # if success, jump past end
			procset.append( elset[-1] + (2,1) ) # will cause a failure if last element doesn't match
			procset.append( (None, Fail, Here ) )
			procset.append( (None, Skip, 1) )
			# if the following looks familiar you probably looked at seq above
			if repeating:
				if optional:
					return [ (None, SubTable, ( (None, SubTable, tuple( procset), 2,1), (None, EOF, Here,-1,1) ) ) ]
				else: # not optional
					return [ (None, SubTable, ( (None, SubTable, tuple( procset)),(None, SubTable, tuple( procset), 2,1), (None, EOF, Here,-1,1) ) ) ]
			else: # single
				if optional:
					return [ (None, SubTable, tuple( procset), 1,1) ]
				else: # not optional
					return [ (None, SubTable, tuple( procset) ) ]
		else: # positive
			if repeating:
				if optional:
					procset = []
					for i in range( len( elset)):
						procset.append( elset[i] + (1,-i) ) # if success, go back to start which is -i elements back
					return procset
				else: # not optional
					procset = []
					for i in range( len( elset)-1):
						procset.append( elset[i] + (1, len(elset)-i+1) ) # if success, jump to later section
					procset.append( elset[-1] + ( 1, 2)  ) # will cause a failure if last element doesn't match using an explicit fail command
					procset.append( (None, Fail, Here)  ) # will cause a failure if last element doesn't match using an explicit fail command
					for i in range( len( elset)-1):
						procset.append( elset[i] + (1, -i) ) # if success, go back to start which is -i elements back
					procset.append( elset[-1] + ( 1, 1-(len(elset)) )  ) # will cause a failure if last element doesn't match using an explicit fail command
					return procset
			else: # single
				if optional:
					procset = []
					for i in range( len( elset)):
						procset.append( elset[i] + (1,len(elset)-i) ) # if success, jump past end
					return procset
				else: # not optional
					procset = []
					for i in range( len( elset) -1): # note that we have to treat last el specially
						procset.append( elset[i] + (1,len(elset)-i) ) # if success, jump past end
					procset.append( elset[-1] ) # will cause a failure if last element doesn't match
					return procset
		
	def name( self, value, genobj, negative = None, repeating = None, optional = None, reportname=None ):
		svalue = genobj.stringval( value )
		try:
			sindex = genobj.nameset.index( svalue )
		except ValueError: # eeps, a value not declared
			try:
				sindex = genobj.nameset.index( '<'+svalue+'>' )
				svalue = None
			except ValueError:
				err( '''The name %s could not be found in the declarationset. The parser will not compile.'''%svalue)
				genobj.nameset.append( svalue )
				genobj.tupleset.append( None )
				sindex = len( genobj.nameset) - 1
		if negative:
			if repeating:
				if optional:
					return [ (svalue, SubTable, ( (None, TableInList, (genobj.tupleset, sindex), 1,3), (None, EOF, Here,1,2), (None,Skip,1,-2,-2) ) ) ]
				else: # not optional
					return [ (svalue, SubTable, ( (None, TableInList, (genobj.tupleset, sindex),2,1),(None, Fail, Here),(None, Skip, 1), (None, TableInList, (genobj.tupleset, sindex), 1,3), (None, EOF, Here,1,2), (None,Skip,1,-2,-2) ) ) ]
			else: # single
				if optional:
					return [ (None, SubTable, ( (None, TableInList, (genobj.tupleset, sindex),2,1),(None, Fail, Here),(svalue, Skip, 1) ),1,1) ]
				else: # not optional
					return [ (None, SubTable, ( (None, TableInList, (genobj.tupleset, sindex),2,1),(None, Fail, Here),(svalue, Skip, 1) )) ]
		else: # positive
			if repeating:
				if optional:
					return [ (svalue, TableInList, (genobj.tupleset, sindex), 1,0) ]
				else: # not optional
					return [ (svalue, TableInList, (genobj.tupleset, sindex)), (svalue, TableInList, (genobj.tupleset, sindex),1,0) ]
			else: # single
				if optional:
					return [ (svalue, TableInList, (genobj.tupleset, sindex), 1,1) ]
				else: # not optional
					return [ (svalue, TableInList, (genobj.tupleset, sindex)) ]
	specialescapedmap = {
	'a':'\a',
	'b':'\b',
	'f':'\f',
	'n':'\n',
	'r':'\r',
	't':'\t',
	'v':'\v',
	'\\':'\\',
	'"':'"',
	"'":"'",
	}
	
	def escapedchar( self, el, genobj ):
		svalue = ''
		if el[3][0][0] == 'SPECIALESCAPEDCHAR':
			svalue = svalue + self.specialescapedmap[ genobj.stringval( el[3][0] ) ]
		elif el[3][0][0] == 'OCTALESCAPEDCHAR':
			#print 'OCTALESCAPEDCHAR', genobj.stringval( el)
			ovnum = 0
			ovpow = 0
			ov = genobj.stringval( el[3][0] )
			while ov:
				ovnum = ovnum + int( ov[-1] ) * (8**ovpow)
				ovpow = ovpow + 1
				ov = ov[:-1]
			svalue = svalue + chr( ovnum )
			#print 'svalue ', `svalue`
		return svalue
		
	
	def literal( self, value, genobj, negative = None, repeating=None, optional=None, reportname=None ):
		'''
		Calculate the tag-table for a literal element token
		'''
		svalue = ''
		for el in value[3]:
			if el[0] in ('CHARNOSNGLQUOTE', 'CHARNODBLQUOTE'):
				svalue = svalue+genobj.stringval( el )
			elif el[0] == 'ESCAPEDCHAR':
				svalue = svalue + self.escapedchar( el, genobj )
		#print 'literal value', `genobj.stringval( value )`
		#print '       svalue', `svalue`
		# svalue = svalue[1:-1]
		if negative:
			if repeating: # a repeating negative value, a "search" in effect
				if optional: # if fails, then go to end of file
					return [ (None, sWordStart, BMS( svalue ),1,2), (None, Move, ToEOF ) ]
				else: # must first check to make sure the current position is not the word, then the same
					return [ (None, Word, svalue, 2,1),(None, Fail, Here),(None, sWordStart, BMS( svalue ),1,2), (None, Move, ToEOF ) ]
					#return [ (None, Word, svalue, 2,1),(None, Fail, Here),(None, WordStart, svalue,1,2), (None, Move, ToEOF ) ]
			else: # a single-character test saying "not a this"
				if optional: # test for a success, move back if success, move one forward if failure
					if len(svalue) > 1:
						return [ (None, Word, svalue, 2,1), 
							(None, Skip, -len(svalue), 2,2), # backup if this was the word to start of word, succeed
							(None, Skip, 1 ) ] # else just move one character and succeed
					else: # Uses Is test instead of Word test, should be faster I'd imagine
						return [ (None, Is, svalue, 2,1), 
							(None, Skip, -1, 2,2), # backtrack
							(None, Skip, 1 ) ] # else just move one character and succeed
				else: # must find at least one character not part of the word, so
					if len(svalue) > 1:
						return [ (None, Word, svalue, 2,1), 
							(None, Fail, Here),
							(None, Skip, 1 ) ] # else just move one character and succeed
					else: #must fail if it finds or move one forward
						return [ (None, Is, svalue, 2,1), 
							(None, Fail, Here),
							(None, Skip, 1 ) ] # else just move one character and succeed
		else: # positive
			if repeating:
				if optional:
					if len(svalue) > 1:
						return [ (None, Word, svalue, 1,0) ]
					else:
						return [ (None, Is, svalue, 1,0) ]
				else: # not optional
					if len(svalue) > 1:
						return [ (None, Word, svalue),(None, Word, svalue,1,0) ]
					else:
						return [ (None, Is, svalue),(None, Is, svalue,1,0) ]
			else: # not repeating
				if optional:
					if len(svalue) > 1:
						return [ (None, Word, svalue, 1,1) ]
					else:
						return [ (None, Is, svalue, 1,1) ]
				else: # not optional
					if len(svalue) > 1:
						return [ (None, Word, svalue) ]
					else:
						return [ (None, Word, svalue) ]
	
	def charnobrace( self, cval, genobj ):
		#print 'cval', cval
		if cval[3][0][0] == 'ESCAPEDCHAR':
			return self.escapedchar( cval[3][0], genobj )
		#print '''Straight non-brace character''', `genobj.stringval( cval[3][0] )`
		return genobj.stringval( cval )
	def range( self, value, genobj, negative = None, repeating=None, optional=None, reportname=None ):
		dataset = []
		for cval in value[3]:
			if cval[0] == 'CHARBRACE':
				dataset.append( ']')
			elif cval[0] == 'CHARDASH':
				dataset.append( '-')
			elif cval[0] == 'CHARNOBRACE':
				dataset.append( self.charnobrace( cval, genobj ) )
			elif cval[0] == 'CHARRANGE':
				start = ord( self.charnobrace( cval[3][0], genobj ) )
				end = ord( self.charnobrace( cval[3][1], genobj ) )
				if start < end:
					dataset.append(  string.join( map( chr, range( start, end +1 ) ), '' )  )
				else:
					dataset.append(  string.join( map( chr, range( end, start +1 ) ), '' )  )
			else:
				dataset.append( genobj.stringval( cval ) )
		if negative:
			#svalue = set( string.join( dataset, '' ), 0 )
			svalue = string.join( dataset, '' )
		else:
			#svalue = set( string.join( dataset, '' ), 1)
			svalue = string.join( dataset, '' )
		if negative:
			if repeating:
				if optional:
					#return [ (None, AllInSet, svalue, 1 ) ]
					return [ (None, AllNotIn, svalue, 1 ) ]
				else: # not optional
					#return [ (None, AllInSet, svalue ) ]
					return [ (None, AllNotIn, svalue ) ]
			else: # not repeating
				if optional:
					#return [ (None, IsInSet, svalue, 1 ) ]
					return [ (None, IsNotIn, svalue, 1 ) ]
				else: # not optional
					#return [ (None, IsInSet, svalue ) ]
					return [ (None, IsNotIn, svalue ) ]
		else:
			if repeating:
				if optional:
					#return [ (None, AllInSet, svalue, 1 ) ]
					return [ (None, AllIn, svalue, 1 ) ]
				else: # not optional
					#return [ (None, AllInSet, svalue ) ]
					return [ (None, AllIn, svalue ) ]
			else: # not repeating
				if optional:
					#return [ (None, IsInSet, svalue, 1 ) ]
					return [ (None, IsIn, svalue, 1 ) ]
				else: # not optional
					#return [ (None, IsInSet, svalue ) ]
					return [ (None, IsIn, svalue ) ]

class Generator( _BaseGenerator ):
	def __init__( self, syntaxstring , parser ):
		self.syntaxstring = syntaxstring
		self.parsetree = [0,1,2, tag( syntaxstring, parser )[1] ]
		self.nameset = []
		self.tupleset = []

def buildParser( declaration, prebuiltnodes=() ):
	'''
	End-developer function to create an application-specific parser
	the parsing tuple is available on the returned object as
	object.parserbyname( 'declaredname' ), where declaredname is the
	name you defined in your language defintion file.
	
	The declaration argument is the text of a language defintion file.
	'''
	proc = _BaseGenerator( )
	proc.build()
	newgen = Generator( declaration, proc.parserbyname( 'declarationset' ) )
	newgen.build( prebuiltnodes=prebuiltnodes )
	return newgen


